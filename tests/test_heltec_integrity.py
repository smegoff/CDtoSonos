import io
import json
from pathlib import Path
import struct
import sys
import threading
import unittest
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
import heltec_integrity as integrity

try:
    import av
except ImportError:
    av = None


def reference_crc(data, width, polynomial):
    value = 0
    for byte in data:
        value ^= byte << (width - 8)
        for _ in range(8):
            value = ((value << 1) ^ (polynomial if value & (1 << (width - 1)) else 0)) & ((1 << width) - 1)
    return value


def frame(number=0):
    if number < 128:
        encoded = bytes([number])
    else:
        encoded = bytes([0xc0 | (number >> 6), 0x80 | (number & 63)])
    header = b'\xff\xf8\x79\x18' + encoded + b'\x03\xff'
    header += bytes([reference_crc(header, 8, 7)])
    # Two constant subframes. Unequal channel values exercise stereo decoding.
    body = header + b'\x00' + struct.pack('>h', 1000) + b'\x00' + struct.pack('>h', -2000)
    return body + reference_crc(body, 16, 0x8005).to_bytes(2, 'big')


def stream(frames):
    info = struct.pack('>HH', 1024, 1024) + b'\0' * 6
    info += ((44100 << 44) | (1 << 41) | (15 << 36)).to_bytes(8, 'big') + b'\0' * 16
    return b'fLaC\x80\x00\x00\x22' + info + b''.join(frames)


class IntegrityTests(unittest.TestCase):
    def test_standard_crc_vectors(self):
        self.assertEqual(integrity.crc8(b'123456789'), 0xf4)
        self.assertEqual(integrity.crc16(b'123456789'), 0xfee8)
        data = bytes(range(256)) * 10
        self.assertEqual(integrity.crc16(data), reference_crc(data, 16, 0x8005))

    def test_valid_frame_numbers(self):
        for number in (0, 1, 127, 128, 1024, 2047):
            self.assertEqual(integrity.check_frame(frame(number)), number)

    def test_header_and_payload_corruption(self):
        header = bytearray(frame())
        header[7] ^= 1
        with self.assertRaisesRegex(integrity.IntegrityError, 'header_crc8'):
            integrity.check_frame(header)
        payload = bytearray(frame())
        payload[10] ^= 1
        with self.assertRaisesRegex(integrity.IntegrityError, 'frame_crc16'):
            integrity.check_frame(payload)

    def test_truncation_and_profile_fail_closed(self):
        for size in range(11):
            with self.assertRaises(integrity.IntegrityError):
                integrity.check_frame(frame()[:size])
        wrong = bytearray(frame())
        wrong[3] = 0x28
        with self.assertRaisesRegex(integrity.IntegrityError, 'unexpected_frame_profile'):
            integrity.check_frame(wrong)

    def test_credentials_and_unexpected_paths_rejected(self):
        for value in ('http://user:secret@localhost', 'http://localhost/?password=x', 'http://localhost/private'):
            with self.assertRaises(Exception):
                integrity.stream_url(value)
        self.assertEqual(integrity.stream_url('http://192.168.10.161/'), 'http://192.168.10.161:8000/cd.flac')

    def test_missing_health_not_assumed_clean(self):
        result = integrity.health_summary({}, {})
        self.assertFalse(result['available'])
        self.assertIsNone(result['loss_counter_deltas']['overruns'])

    def test_health_deltas_and_reset(self):
        before = {'status': dict.fromkeys(integrity.LOSS_COUNTERS, 1) | {'uptime_s': 100}}
        after = {'status': dict.fromkeys(integrity.LOSS_COUNTERS, 1) | {'uptime_s': 130, 'overruns': 3}}
        self.assertEqual(integrity.health_summary(before, after)['loss_counter_deltas']['overruns'], 2)
        after['status']['uptime_s'] = 5
        result = integrity.health_summary(before, after)
        self.assertTrue(result['reset_detected'])
        self.assertFalse(result['available'])

    def test_reader_deadline_and_memory_bound(self):
        reader = integrity.BoundedReader(io.BytesIO(b'a' * 100000), float('inf'))
        self.assertEqual(len(reader.read(1000000)), 65536)
        reader.deadline = 0
        self.assertEqual(reader.read(10), b'')
        self.assertTrue(reader.deadline_reached)


@unittest.skipUnless(av, 'PyAV is required for end-to-end decoder tests')
class ReceiverTests(unittest.TestCase):
    def check_fixture(self, frames, losses=0, missing_health=False):
        audio = stream(frames)
        counter = [0]
        class Handler(BaseHTTPRequestHandler):
            def log_message(self, *args):
                pass

            def do_GET(self):
                if self.path == '/api/status':
                    counter[0] += 1
                    state = dict.fromkeys(integrity.LOSS_COUNTERS, 0)
                    state.update(uptime_s=100 + counter[0], overruns=losses if counter[0] > 1 else 0)
                    body = json.dumps({} if missing_health else state).encode()
                else:
                    body = audio
                self.send_response(200)
                self.send_header('Content-Length', str(len(body)))
                self.end_headers()
                self.wfile.write(body)
        server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        base = f'http://127.0.0.1:{server.server_port}'
        try:
            with patch.object(integrity, 'stream_url', return_value=base + '/cd.flac'):
                return integrity.run_check(base, seconds=1, timeout=1)
        finally:
            server.shutdown()
            server.server_close()
            thread.join(timeout=2)

    def test_clean_decode_and_crc(self):
        result = self.check_fixture([frame(i) for i in range(64)])
        self.assertEqual(result['result'], 'pass', result)
        self.assertEqual(result['crc_checked_frames'], 44)
        self.assertEqual(result['decoded_frames'], 44)

    def test_corrupt_payload_fails_not_false_pass(self):
        frames = [frame(i) for i in range(64)]
        bad = bytearray(frames[20])
        bad[10] ^= 1
        frames[20] = bytes(bad)
        result = self.check_fixture(frames)
        self.assertEqual(result['result'], 'fail', result)

    def test_corrupt_header_fails_not_false_pass(self):
        frames = [frame(i) for i in range(64)]
        bad = bytearray(frames[20])
        bad[7] ^= 1
        frames[20] = bytes(bad)
        self.assertEqual(self.check_fixture(frames)['result'], 'fail')

    def test_missing_frame_detected(self):
        result = self.check_fixture([frame(i) for i in range(64) if i != 20])
        self.assertEqual(result['result'], 'fail')
        self.assertEqual(result['error'], 'frame_sequence')

    def test_early_end_not_pass(self):
        result = self.check_fixture([frame(i) for i in range(10)])
        self.assertEqual(result['result'], 'fail')
        self.assertFalse(result['complete'])

    def test_clean_crc_with_device_losses_warns(self):
        result = self.check_fixture([frame(i) for i in range(64)], losses=3)
        self.assertEqual(result['integrity_result'], 'pass')
        self.assertEqual(result['result'], 'warning')

    def test_missing_health_warns(self):
        result = self.check_fixture([frame(i) for i in range(64)], missing_health=True)
        self.assertEqual(result['result'], 'warning')


if __name__ == '__main__':
    unittest.main()
