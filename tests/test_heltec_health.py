"""Standalone stdlib tests: python tests/test_heltec_health.py (no device needed)."""

import argparse
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import unittest
from unittest.mock import Mock, patch
from urllib.error import HTTPError, URLError


SCRIPT_PATH = Path(__file__).resolve().parents[1] / "scripts" / "heltec_health.py"
SPEC = importlib.util.spec_from_file_location("heltec_health", SCRIPT_PATH)
health = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(health)


def snapshot(elapsed, **status):
    return {"elapsed_s": elapsed, "request_ms": 1.0, "status": status}


class HealthTests(unittest.TestCase):
    def test_sanitisation_keeps_only_valid_whitelisted_fields(self):
        clean = health.sanitise_status({
            "version": "0.1.4", "captured_frames": 1024,
            "cpu_percent": float("nan"), "wifi_rssi": float("inf"),
            "heap_free": True, "clock_locked": True, "signal_present": 1,
            "wifi_password": "private", "session": "private",
            "stream_url": "http://user:private@device/cd.flac",
            "unexpected": {"secret": "private"},
        })
        self.assertEqual(clean, {"version": "0.1.4", "captured_frames": 1024,
                                 "clock_locked": True})
        self.assertNotIn("private", json.dumps(clean))
        with self.assertRaises(ValueError):
            health.sanitise_status([])

    def test_endpoint_rejects_credentials_and_non_status_paths(self):
        self.assertEqual(health.status_url("192.0.2.1"), "http://192.0.2.1/api/status")
        self.assertEqual(health.status_url("https://example.invalid/api/status/"),
                         "https://example.invalid/api/status")
        for value in ("http://user:private@example.invalid", "http://example.invalid/?token=private",
                      "http://example.invalid/#private", "http://example.invalid:8000/cd.flac",
                      "ftp://example.invalid", "http://example.invalid:99999"):
            with self.subTest(value=value), self.assertRaises(argparse.ArgumentTypeError):
                health.status_url(value)

    def test_sample_uses_request_midpoint_and_bounded_status_read(self):
        opener = Mock()
        response = io.BytesIO(b'{"captured_frames": 1024, "wifi_password": "private"}')
        opener.open.return_value = response
        with patch.object(health.time, "monotonic", side_effect=[100.2, 100.6]):
            result = health.collect_sample(opener, "http://example.invalid/api/status", 3.0, 100.0)
        self.assertEqual(result["elapsed_s"], 0.4)
        self.assertEqual(result["request_ms"], 400.0)
        self.assertEqual(result["status"], {"captured_frames": 1024})
        request = opener.open.call_args.args[0]
        self.assertEqual(request.full_url, "http://example.invalid/api/status")
        self.assertEqual(opener.open.call_args.kwargs["timeout"], 3.0)
        self.assertTrue(response.closed)

    def test_rates_use_local_elapsed_instead_of_integer_device_uptime(self):
        summary = health.summarise([
            snapshot(0.25, uptime_s=100, captured_frames=1000, stream_frames=10,
                     read_timeouts=2, stream_clients=2, cpu_percent=20),
            snapshot(2.75, uptime_s=102, captured_frames=111250, stream_frames=224,
                     read_timeouts=5, stream_clients=1, cpu_percent=35),
        ])
        self.assertEqual(summary["elapsed_s"], 2.5)
        self.assertEqual(summary["rates"]["capture_pcm_frames_per_s"], 44100.0)
        self.assertEqual(summary["rates"]["stream_pcm_frames_per_s_all_clients"], 87654.4)
        self.assertEqual(summary["counter_deltas"]["read_timeouts"], 3)
        self.assertEqual(summary["ranges"]["cpu_percent"], {"min": 20, "max": 35, "samples": 2})
        self.assertEqual(summary["client_changes"], [{"elapsed_s": 2.75, "from": 2, "to": 1}])

    def test_missing_and_partial_firmware_metrics_are_unavailable_not_zero(self):
        summary = health.summarise([
            snapshot(0, captured_frames=0, read_timeouts=0),
            snapshot(1, captured_frames=44100, read_timeouts=0, dma_queue_overflows=0,
                     capture_age_ms=12),
        ])
        self.assertEqual(summary["counter_deltas"]["read_timeouts"], 0)
        for field in ("dma_queue_overflows", "capture_publish_drops", "clock_loss_events"):
            self.assertIsNone(summary["counter_deltas"][field])
            self.assertIn(field, summary["unavailable_or_partial_metrics"])
        self.assertIsNone(summary["ranges"]["wifi_rssi"])
        self.assertIn("capture_age_ms", summary["unavailable_or_partial_metrics"])
        self.assertEqual(summary["ranges"]["capture_age_ms"], {"min": 12, "max": 12, "samples": 1})
        self.assertIsNone(summary["rates"]["stream_pcm_frames_per_s_all_clients"])

    def test_uptime_or_counter_reset_invalidates_whole_run_rates(self):
        for changed_field in ("uptime_s", "captured_frames", "stream_frames", "read_timeouts"):
            before = {"uptime_s": 100, "captured_frames": 5000, "stream_frames": 10, "read_timeouts": 5}
            after = {"uptime_s": 102, "captured_frames": 93200, "stream_frames": 96, "read_timeouts": 5}
            after[changed_field] = 1
            with self.subTest(changed_field=changed_field):
                summary = health.summarise([snapshot(0, **before), snapshot(2, **after)])
                self.assertEqual(summary["resets"], [{"elapsed_s": 2, "decreased_fields": [changed_field]}])
                self.assertTrue(all(value is None for value in summary["counter_deltas"].values()))
                self.assertTrue(all(value is None for value in summary["rates"].values()))

    def test_http_and_connection_errors_keep_codes_but_exclude_private_text(self):
        errors = (
            (HTTPError("http://example.invalid/api/status", 503, "private reason", {},
                       io.BytesIO(b"private body")),
             {"kind": "http", "status_code": 503, "message": "Service Unavailable"}),
            (URLError("private connection details"), {"kind": "connection", "message": "URLError"}),
        )
        for error, expected in errors:
            opener = Mock()
            opener.open.side_effect = error
            with self.subTest(error=type(error).__name__), patch.object(
                    health.time, "monotonic", side_effect=[1.0, 1.5]):
                sample = health.collect_sample(opener, "http://example.invalid/api/status", 1.0, 1.0)
                self.assertEqual(sample["error"], expected)
                self.assertNotIn("private", json.dumps(sample))
                self.assertNotIn("status", sample)
                self.assertEqual(sample["request_ms"], 500.0)

    def test_invalid_or_oversized_json_becomes_sample_error(self):
        for payload in (b"not-json-private", b"[]", b" " * (health.MAX_RESPONSE_BYTES + 1)):
            opener = Mock()
            opener.open.return_value = io.BytesIO(payload)
            with self.subTest(size=len(payload)):
                sample = health.collect_sample(opener, "http://example.invalid/api/status", 1.0, 0.0)
                self.assertEqual(sample["error"]["kind"], "invalid_response")
                self.assertNotIn("private", json.dumps(sample))
                self.assertNotIn("status", sample)

    def test_cli_fails_for_errors_and_resets_without_contacting_a_device(self):
        good = [snapshot(index / 10, uptime_s=100 + index, captured_frames=1000 + 4410 * index)
                for index in range(3)]
        failed = [good[0], {"elapsed_s": 0.1, "error": {"kind": "http", "status_code": 503,
                   "message": "Service Unavailable"}}, good[2]]
        reset = [good[0], good[1], snapshot(0.2, uptime_s=1, captured_frames=1)]
        for samples, expected_exit in ((good, 0), (failed, 1), (reset, 1)):
            with self.subTest(expected_exit=expected_exit), contextlib.redirect_stdout(io.StringIO()), \
                    contextlib.redirect_stderr(io.StringIO()), \
                    patch.object(health, "build_opener") as opener, \
                    patch.object(health, "collect_sample", side_effect=samples), \
                    patch.object(health.time, "sleep"), \
                    patch.object(health.time, "monotonic", side_effect=[0, 0.05, 0.1, 0.15, 0.2, 0.25, 0.3]):
                self.assertEqual(health.main(["http://example.invalid", "--duration", "0.25",
                                             "--interval", "0.1"]), expected_exit)
                opener.return_value.open.assert_not_called()


if __name__ == "__main__":
    unittest.main(verbosity=2)
