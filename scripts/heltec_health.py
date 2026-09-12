#!/usr/bin/env python3
"""Read-only Heltec performance sampler; never opens an audio or serial connection.

Example: python scripts/heltec_health.py http://192.168.10.123 --duration 120
         --output tmp/heltec-health.json

Rates use local monotonic request midpoints, not the coarse device uptime.
Stream PCM frames/s is FLAC blocks/s * 1024, summed across all stream clients.
Missing firmware metrics are unavailable (JSON null), never assumed to be zero.
"""

import argparse
from datetime import datetime, timezone
from http import HTTPStatus
import json
import math
from pathlib import Path
import socket
import sys
import time
from urllib.error import HTTPError, URLError
from urllib.parse import urlsplit, urlunsplit
from urllib.request import ProxyHandler, Request, build_opener


COUNTERS = (
    "captured_frames", "stream_frames", "read_timeouts", "overruns",
    "dma_queue_overflows", "capture_publish_drops", "clock_loss_events",
    "meter_updates", "spectrum_updates",
)
RANGES = (
    "cpu_percent", "cpu0_percent", "cpu1_percent", "wifi_rssi", "heap_free",
    "source_rate_hz", "capture_age_ms", "capture_stack_free", "analysis_stack_free",
    "analysis_dsp_us", "analysis_dsp_max_us",
)
NUMERIC_FIELDS = set(COUNTERS + RANGES + (
    "uptime_s", "stream_clients", "sample_rate", "bits", "channels", "slot_bits",
    "cpu_mhz", "peak_l", "peak_r", "rms_l", "rms_r", "analysis_runs",
))
BOOL_FIELDS = {
    "wifi_connected", "setup_ap", "lora_enabled", "lora_reset_asserted",
    "clock_locked", "signal_present", "emphasis", "sonos_auto_enabled",
    "sonos_play_commanded",
}
TEXT_FIELDS = {"version", "sonos_auto_state"}
MAX_RESPONSE_BYTES = 65536
FLAC_BLOCK_SAMPLES = 1024


def positive(value):
    number = float(value)
    if not math.isfinite(number) or number <= 0:
        raise argparse.ArgumentTypeError("must be a finite positive number")
    return number


def status_url(value):
    """Accept a device base URL or its status URL, with no embedded secrets."""
    try:
        parts = urlsplit(value if "://" in value else "http://" + value)
        if (parts.scheme not in ("http", "https") or not parts.hostname
                or parts.username is not None or parts.password is not None
                or parts.query or parts.fragment or parts.port == 0):
            raise ValueError
        if parts.path not in ("", "/", "/api/status", "/api/status/"):
            raise ValueError
    except ValueError as error:
        raise argparse.ArgumentTypeError(
            "use a device HTTP(S) base URL or /api/status URL without credentials, "
            "query parameters or a fragment"
        ) from error
    return urlunsplit((parts.scheme, parts.netloc, "/api/status", "", ""))


def sanitise_status(payload):
    if not isinstance(payload, dict):
        raise ValueError("status response must be a JSON object")
    clean = {}
    for key in NUMERIC_FIELDS:
        value = payload.get(key)
        if isinstance(value, (int, float)) and not isinstance(value, bool):
            if math.isfinite(value):
                clean[key] = value
    for key in BOOL_FIELDS:
        if isinstance(payload.get(key), bool):
            clean[key] = payload[key]
    for key in TEXT_FIELDS:
        if isinstance(payload.get(key), str):
            clean[key] = payload[key][:100]
    return clean


def collect_sample(opener, url, timeout, origin):
    started = time.monotonic()
    sample = {}
    try:
        request = Request(url, headers={"Accept": "application/json", "Cache-Control": "no-cache"})
        with opener.open(request, timeout=timeout) as response:
            payload = response.read(MAX_RESPONSE_BYTES + 1)
            if len(payload) > MAX_RESPONSE_BYTES:
                raise ValueError("status response exceeded size limit")
            sample["status"] = sanitise_status(json.loads(payload))
    except HTTPError as error:
        try:
            reason = HTTPStatus(error.code).phrase
        except ValueError:
            reason = "HTTP error"
        sample["error"] = {"kind": "http", "status_code": error.code, "message": reason}
        error.close()
    except (URLError, TimeoutError, socket.timeout, OSError) as error:
        # Do not log arbitrary server text, response bodies or URLs in exceptions.
        sample["error"] = {"kind": "connection", "message": type(error).__name__}
    except (ValueError, UnicodeDecodeError) as error:
        sample["error"] = {"kind": "invalid_response", "message": type(error).__name__}
    finished = time.monotonic()
    sample["elapsed_s"] = round((started + finished) / 2 - origin, 6)
    sample["request_ms"] = round((finished - started) * 1000, 3)
    return sample


def summarise(samples):
    good = [sample for sample in samples if "status" in sample]
    errors = [sample for sample in samples if "error" in sample]
    elapsed = good[-1]["elapsed_s"] - good[0]["elapsed_s"] if len(good) >= 2 else 0
    resets = []
    client_changes = []
    for previous, current in zip(good, good[1:]):
        before, after = previous["status"], current["status"]
        decreased = [field for field in ("uptime_s",) + COUNTERS
                     if field in before and field in after and after[field] < before[field]]
        if decreased:
            resets.append({"elapsed_s": current["elapsed_s"], "decreased_fields": decreased})
        if ("stream_clients" in before and "stream_clients" in after
                and before["stream_clients"] != after["stream_clients"]):
            client_changes.append({"elapsed_s": current["elapsed_s"],
                                   "from": before["stream_clients"], "to": after["stream_clients"]})

    deltas = {}
    unavailable = []
    for field in COUNTERS:
        present = good and all(field in sample["status"] for sample in good)
        if not present:
            unavailable.append(field)
        deltas[field] = (good[-1]["status"][field] - good[0]["status"][field]
                         if present and len(good) >= 2 and not resets else None)

    ranges = {}
    for field in RANGES:
        values = [sample["status"][field] for sample in good if field in sample["status"]]
        ranges[field] = {"min": min(values), "max": max(values), "samples": len(values)} if values else None
        if len(values) != len(good) or not good:
            unavailable.append(field)

    rates = {}
    for label, counter, multiplier in (
        ("capture_pcm_frames_per_s", "captured_frames", 1),
        ("stream_pcm_frames_per_s_all_clients", "stream_frames", FLAC_BLOCK_SAMPLES),
        ("meter_updates_per_s", "meter_updates", 1),
        ("spectrum_updates_per_s", "spectrum_updates", 1),
    ):
        rates[label] = (round(deltas[counter] * multiplier / elapsed, 3)
                        if deltas[counter] is not None and elapsed > 0 else None)
    booleans = {}
    for field in ("clock_locked", "signal_present", "wifi_connected"):
        values = [sample["status"][field] for sample in good if field in sample["status"]]
        booleans[field] = {"true_samples": values.count(True), "false_samples": values.count(False)} if values else None
    clients = [sample["status"]["stream_clients"] for sample in good if "stream_clients" in sample["status"]]
    return {
        "successful_samples": len(good), "failed_samples": len(errors),
        "elapsed_s": round(elapsed, 6), "resets": resets,
        "counter_deltas": deltas, "rates": rates, "ranges": ranges,
        "boolean_observations": booleans,
        "stream_clients": {"initial": clients[0], "final": clients[-1],
                           "min": min(clients), "max": max(clients)} if clients else None,
        "client_changes": client_changes, "unavailable_or_partial_metrics": sorted(unavailable),
        "notes": ["Stream rate sums every client; one FLAC block represents 1024 PCM frames.",
                  "Rates use local monotonic request midpoints; device uptime is only used to detect resets.",
                  "Any observed counter decrease invalidates whole-run deltas and rates.",
                  "Polling cannot detect every event between samples; sample transport adds timing uncertainty.",
                  "capture_age_ms 4294967295 means no PCM block has been published."],
    }


def display(summary):
    print("Heltec health: {} successful / {} failed samples over {:.3f} s".format(
        summary["successful_samples"], summary["failed_samples"], summary["elapsed_s"]))
    for key, value in summary["rates"].items():
        print("  {}: {}".format(key, "unavailable" if value is None else value))
    for key, value in summary["counter_deltas"].items():
        print("  delta {}: {}".format(key, "unavailable" if value is None else value))
    for key, values in summary["ranges"].items():
        print("  {}: {}".format(key, "unavailable" if values is None else
                               "{} .. {}".format(values["min"], values["max"])))
    print("  resets: {}; client changes: {}".format(len(summary["resets"]), len(summary["client_changes"])))
    if summary["stream_clients"]:
        print("  stream clients: {}".format(json.dumps(summary["stream_clients"], sort_keys=True)))
    for key, values in summary["boolean_observations"].items():
        print("  {}: {}".format(key, "unavailable" if values is None else json.dumps(values, sort_keys=True)))
    if summary["unavailable_or_partial_metrics"]:
        print("  unavailable/partial metrics: " + ", ".join(summary["unavailable_or_partial_metrics"]))


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("url", type=status_url, help="device base URL or /api/status URL")
    parser.add_argument("--duration", type=positive, default=120.0, help="sampling duration in seconds (default: 120)")
    parser.add_argument("--interval", type=positive, default=2.0, help="poll interval in seconds (default: 2)")
    parser.add_argument("--timeout", type=positive, default=3.0, help="HTTP timeout in seconds, at most 30 (default: 3)")
    parser.add_argument("--output", type=Path, help="optional JSON output, including whitelisted samples and summary")
    args = parser.parse_args(argv)
    if args.timeout > 30:
        parser.error("--timeout must not exceed 30 seconds")
    started_at = datetime.now(timezone.utc).isoformat()
    origin = time.monotonic()
    deadline = origin + args.duration
    next_sample = origin
    samples = []
    interrupted = False
    # Never send LAN status through an environment-configured HTTP proxy.
    opener = build_opener(ProxyHandler({}))
    try:
        while True:
            time.sleep(max(0, next_sample - time.monotonic()))
            sample = collect_sample(opener, args.url, args.timeout, origin)
            samples.append(sample)
            if "error" in sample:
                print("Status sample at {:.3f} s failed: {}".format(
                    sample["elapsed_s"], json.dumps(sample["error"], sort_keys=True)), file=sys.stderr, flush=True)
            now = time.monotonic()
            if now >= deadline:
                break
            next_sample = min(deadline, max(next_sample + args.interval, now))
    except KeyboardInterrupt:
        interrupted = True
    summary = summarise(samples)
    display(summary)
    if args.output:
        report = {"schema_version": 1, "endpoint": args.url, "started_at_utc": started_at,
                  "requested_duration_s": args.duration, "interval_s": args.interval,
                  "timeout_s": args.timeout, "interrupted": interrupted,
                  "samples": samples, "summary": summary}
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, indent=2, allow_nan=False) + "\n", encoding="utf-8")
        print("JSON report: {}".format(args.output))
    if interrupted:
        return 130
    if summary["failed_samples"] or summary["resets"] or summary["successful_samples"] < 2:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
