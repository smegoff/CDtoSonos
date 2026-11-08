#!/usr/bin/env python3
"""On-demand PC-side CRC/decode check of the current Heltec FLAC profile.

Requires PyAV: python -m pip install av==18.1.0
Never controls Sonos, changes firmware, or saves received audio.
"""
import argparse
from datetime import datetime, timezone
import io
import json
from pathlib import Path
import time
from urllib.parse import urlsplit, urlunsplit
from urllib.request import HTTPRedirectHandler, ProxyHandler, Request, build_opener

from heltec_health import collect_sample, positive, status_url


RATE = 44100
BLOCK = 1024
LOSS_COUNTERS = ('overruns', 'dma_queue_overflows', 'capture_publish_drops', 'clock_loss_events')


class IntegrityError(ValueError):
    """A fixed, safe diagnostic code; never contains audio or server text."""


def crc_table(width, polynomial):
    table = []
    mask = (1 << width) - 1
    for byte in range(256):
        value = byte << (width - 8)
        for _ in range(8):
            value = ((value << 1) ^ (polynomial if value & (1 << (width - 1)) else 0)) & mask
        table.append(value)
    return tuple(table)


CRC8 = crc_table(8, 0x07)
CRC16 = crc_table(16, 0x8005)


def crc8(data):
    value = 0
    for byte in data:
        value = CRC8[value ^ byte]
    return value


def crc16(data):
    value = 0
    for byte in data:
        value = ((value << 8) & 0xffff) ^ CRC16[(value >> 8) ^ byte]
    return value


def check_frame(data):
    """Validate one complete demuxed frame, explicitly scoped to our encoder.

    This is not a general FLAC parser: profile changes fail closed. PyAV finds
    frame boundaries; this independent implementation checks the frame bytes.
    """
    if len(data) < 11 or len(data) > 4144:
        raise IntegrityError('frame_size')
    if data[:4] != b'\xff\xf8\x79\x18':
        raise IntegrityError('unexpected_frame_profile')
    first = data[4]
    if first < 0x80:
        count, number = 1, first
    else:
        count = 0
        bit = 0x80
        while first & bit:
            count += 1
            bit >>= 1
        if not 2 <= count <= 6:
            raise IntegrityError('frame_number_encoding')
        number = first & ((1 << (7 - count)) - 1)
        if len(data) < 4 + count + 5:
            raise IntegrityError('truncated_header')
        for byte in data[5:4 + count]:
            if byte & 0xc0 != 0x80:
                raise IntegrityError('frame_number_encoding')
            number = (number << 6) | (byte & 0x3f)
        minimum = (0, 0, 0x80, 0x800, 0x10000, 0x200000, 0x4000000)[count]
        if number < minimum or number > 0x7fffffff:
            raise IntegrityError('frame_number_encoding')
    header_end = 4 + count + 2
    if crc8(data[:header_end]) != data[header_end]:
        raise IntegrityError('header_crc8')
    if int.from_bytes(data[4 + count:header_end], 'big') + 1 != BLOCK:
        raise IntegrityError('unexpected_block_size')
    if crc16(data[:-2]) != int.from_bytes(data[-2:], 'big'):
        raise IntegrityError('frame_crc16')
    return number


class NoRedirect(HTTPRedirectHandler):
    def redirect_request(self, *args, **kwargs):
        return None


class BoundedReader(io.RawIOBase):
    """Bound network reads and stop even if a peer sends endless junk."""
    def __init__(self, response, deadline):
        self.response = response
        self.deadline = deadline
        self.bytes_received = 0
        self.max_read_ms = 0.0
        self.deadline_reached = False

    def readable(self):
        return True

    def seekable(self):
        return False

    def read(self, size=-1):
        if time.monotonic() >= self.deadline:
            self.deadline_reached = True
            return b''
        size = 65536 if size < 0 else min(size, 65536)
        started = time.monotonic()
        data = self.response.read(size)
        self.max_read_ms = max(self.max_read_ms, (time.monotonic() - started) * 1000)
        self.bytes_received += len(data)
        return data


def stream_url(base):
    parts = urlsplit(status_url(base))
    if parts.scheme != 'http':
        raise argparse.ArgumentTypeError('the Heltec stream uses HTTP, not HTTPS')
    host = f'[{parts.hostname}]' if ':' in parts.hostname else parts.hostname
    return urlunsplit(('http', host + ':8000', '/cd.flac', '', ''))


def health_summary(before, after):
    a, b = before.get('status', {}), after.get('status', {})
    reset = ('uptime_s' in a and 'uptime_s' in b and b['uptime_s'] < a['uptime_s'])
    reset = reset or any(key in a and key in b and b[key] < a[key] for key in LOSS_COUNTERS)
    deltas = {key: b[key] - a[key] if key in a and key in b and not reset else None
              for key in LOSS_COUNTERS}
    return {'reset_detected': reset, 'loss_counter_deltas': deltas,
            'available': all(value is not None for value in deltas.values())}


def inspect_packets(container, seconds, report):
    audio = container.streams.audio[0]
    if audio.codec_context.name != 'flac':
        raise IntegrityError('unexpected_codec')
    audio.codec_context.options = {'err_detect': 'crccheck+explode'}
    expected_number = 0
    for packet in container.demux(audio):
        if not packet.size:
            continue
        data = bytes(packet)
        number = check_frame(data)
        report['crc_checked_frames'] += 1
        if number != expected_number:
            raise IntegrityError('frame_sequence')
        expected_number += 1
        decoded = 0
        for frame in packet.decode():
            if (frame.sample_rate != RATE or frame.format.name != 's16'
                    or len(frame.layout.channels) != 2 or frame.samples != BLOCK):
                raise IntegrityError('unexpected_decoded_format')
            report['decoded_samples_per_channel'] += frame.samples
            report['decoded_frames'] += 1
            decoded += 1
        if decoded != 1:
            raise IntegrityError('unexpected_decoder_frame_count')
        if report['decoded_samples_per_channel'] >= seconds * RATE:
            report['complete'] = True
            break


def run_check(base, seconds=30, timeout=5):
    # Import lazily so CRC and CLI validation tests do not require PyAV.
    import av
    av.logging.set_level(av.logging.PANIC)
    endpoint = status_url(base)
    source = stream_url(base)
    opener = build_opener(ProxyHandler({}), NoRedirect())
    origin = time.monotonic()
    report = {'schema_version': 1, 'started_at_utc': datetime.now(timezone.utc).isoformat(),
              'device': endpoint, 'stream': source, 'pyav_version': av.__version__,
              'requested_audio_seconds': seconds, 'complete': False,
              'crc_checked_frames': 0, 'decoded_frames': 0,
              'decoded_samples_per_channel': 0, 'error': None,
              'crc8_failures': 0, 'crc16_failures': 0, 'sequence_errors': 0,
              'format_errors': 0, 'decode_errors': 0, 'transport_errors': 0}
    report['health_before'] = collect_sample(opener, endpoint, timeout, origin)
    reader = None
    started = time.monotonic()
    try:
        request = Request(source, headers={'Accept': 'audio/flac'})
        with opener.open(request, timeout=timeout) as response:
            reader = BoundedReader(response, started + seconds + 15)
            with av.open(reader, mode='r', format='flac',
                         options={'err_detect': 'crccheck+explode', 'probesize': '32768',
                                  'analyzeduration': '1000000'}) as container:
                inspect_packets(container, seconds, report)
        if not report['complete']:
            report['error'] = 'deadline' if reader.deadline_reached else 'early_end'
    except IntegrityError as error:
        code = str(error)
        report['error'] = code
        key = {'header_crc8': 'crc8_failures', 'frame_crc16': 'crc16_failures',
               'frame_sequence': 'sequence_errors'}.get(code, 'format_errors')
        report[key] += 1
    except av.error.FFmpegError:
        report['decode_errors'] += 1
        report['error'] = 'demux_or_decode_error'
    except (OSError, ValueError, IndexError):
        report['transport_errors'] += 1
        report['error'] = 'connection_or_response_error'
    except KeyboardInterrupt:
        report['error'] = 'cancelled'
    report['wall_seconds'] = round(time.monotonic() - started, 3)
    report['audio_seconds'] = round(report['decoded_samples_per_channel'] / RATE, 6)
    report['bytes_received'] = reader.bytes_received if reader else 0
    report['max_http_read_ms'] = round(reader.max_read_ms, 2) if reader else None
    report['health_after'] = collect_sample(opener, endpoint, timeout, origin)
    report['health'] = health_summary(report['health_before'], report['health_after'])
    report['integrity_result'] = 'pass' if report['complete'] and report['error'] is None else 'fail'
    report['result'] = report['integrity_result']
    if report['result'] == 'pass' and (not report['health']['available']
            or report['health']['reset_detected']
            or any(value for value in report['health']['loss_counter_deltas'].values())):
        report['result'] = 'warning'
    report['limitations'] = [
        'Checks the received Heltec FLAC profile, not Sonos decoder feedback or original-disc bit accuracy.',
        'Bad input samples and source blocks omitted before encoding can have valid CRCs.',
        'Device loss counters cover all consumers; they cannot identify which listener fell behind.',
        'HTTP read latency includes buffering and is not a count of audible playback underruns.',
        'Frame boundaries come from FFmpeg; malformed bytes discarded by its parser are not directly counted.',
        'Stops at the first detected error; counts are observations, not a full census of corrupt frames.',
        'The test opens one extra listener temporarily; audio is discarded, never saved.',
    ]
    return report


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('device', type=status_url, help='Heltec base URL, without credentials')
    parser.add_argument('--duration', type=positive, default=30, help='audio seconds, 1–300 (default 30)')
    parser.add_argument('--timeout', type=positive, default=5, help='HTTP timeout, 1–10 seconds')
    parser.add_argument('--output', type=Path, help='write a JSON report (no audio)')
    args = parser.parse_args(argv)
    if not 1 <= args.duration <= 300 or not 1 <= args.timeout <= 10:
        parser.error('duration must be 1–300 and timeout 1–10 seconds')
    try:
        stream_url(args.device)
    except argparse.ArgumentTypeError as error:
        parser.error(str(error))
    print('Checking one temporary FLAC listener; no audio is saved.', flush=True)
    try:
        report = run_check(args.device, args.duration, args.timeout)
    except ImportError:
        print('Install the PC-only dependency: python -m pip install av==18.1.0')
        return 2
    print(f"{report['result'].upper()}: {report['crc_checked_frames']} frames checked, "
          f"{report['audio_seconds']:.2f} seconds decoded")
    print('CRC-8 errors: {}; CRC-16 errors: {}; sequence errors: {}; decode errors: {}'.format(
        report['crc8_failures'], report['crc16_failures'], report['sequence_errors'], report['decode_errors']))
    print('Device loss counter changes: ' + json.dumps(report['health']['loss_counter_deltas']))
    if report['error']:
        print('Stopped: ' + report['error'])
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, indent=2, allow_nan=False) + '\n', encoding='utf-8')
        print('Report: ' + str(args.output))
    return 0 if report['result'] == 'pass' else 1


if __name__ == '__main__':
    raise SystemExit(main())
