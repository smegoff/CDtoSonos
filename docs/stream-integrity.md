# On-demand FLAC integrity test

This optional tool runs on a **PC**, not on the Heltec. It opens one temporary
HTTP FLAC listener, checks the encoded frames, decodes them and discards the
audio. It saves a JSON diagnostic report only. There is no firmware change,
background service, scheduled task or Sonos control command.

The extra listener does use Heltec encoder/Wi-Fi capacity while running.
Start with the default 30 seconds, especially on a weak wireless connection.

## Windows setup (once)

From the project directory:

```powershell
python -m venv .venv-integrity
.\.venv-integrity\Scripts\python.exe -m pip install -r scripts\requirements-integrity.txt
```

## Run when required

Keep the CD playing. From the project directory:

```powershell
.\scripts\Test-HeltecStream.ps1 -Device http://192.168.10.161
```

If local policy prevents running PowerShell scripts, call Python directly
without changing that policy:

```powershell
.\.venv-integrity\Scripts\python.exe scripts\heltec_integrity.py http://192.168.10.161 --duration 30 --output output\diagnostics\integrity.json
```

The launcher uses a timestamped report name. The Python command writes the
specified report path. `--duration` accepts 1–300 audio seconds. HTTP operations
time out after five seconds by default; a wall-clock deadline prevents an
endless stream of junk from keeping the test running indefinitely. Cancellation
closes the listener. Test duration may exceed the requested audio duration due
to initial buffering and the two device-status requests.

On other platforms, create a virtual environment, install the same requirement
and run `python scripts/heltec_integrity.py DEVICE --duration 30`.

## What is checked

- Independently recomputed FLAC header CRC-8 and whole-frame CRC-16.
- Contiguous frame numbers, starting at zero for this Heltec's new connection.
- Full FLAC decoding, with strict decoder error checks enabled.
- The expected native stream format: 44.1 kHz, signed 16-bit stereo, 1024
  samples per frame. A different encoder profile fails closed.
- Changes in the Heltec's ring-overrun, DMA-loss, capture-publication-loss and
  clock-interruption counters before/after the test. Missing counters are
  unavailable, never treated as zero; a detected reboot invalidates deltas.

**PASS** means the requested audio duration passed CRC/sequence/decode checks
and the observed device-loss counters did not increase. **WARNING** means
received-frame integrity passed but device counters increased, reset, or were
unavailable. **FAIL** means an integrity/format/decode error, incomplete stream,
connection failure or cancellation occurred. Non-PASS outcomes return exit 1;
setup/argument errors return exit 2. Testing stops at the first detected error.

## What this cannot prove

- It does not receive CRC pass/fail feedback from Sonos.
- Wrong samples at the NAD-to-Heltec input can still be encoded with valid
  CRCs. This cannot establish original-disc bit accuracy or signal integrity.
- Source blocks omitted before encoding can retain contiguous transmitted
  frame numbers. Device loss counters remain important.
- Device counters aggregate all consumers, so a change cannot identify which
  listener fell behind. Counter snapshots do not detect every intervening event.
- PyAV/FFmpeg identifies frame boundaries; malformed bytes it discards are not
  counted directly. Header/payload corruption and missing-frame fixtures are
  tested, but this is not a forensic capture of every received byte.
- Network-read latency includes buffering; it is not an audible-underrun count.
- A clean short test is not a guarantee of long-term playback stability.

CRC layout follows [RFC 9639](https://www.rfc-editor.org/rfc/rfc9639.html#section-9.1.8).
Decoding uses [PyAV](https://pyav.org/docs/stable/api/container.html); the tested
PC dependency is pinned to 18.1.0. No credentials or audio payloads are logged.

## Regression tests

```powershell
.\.venv-integrity\Scripts\python.exe -m unittest discover -s tests -p test_heltec_integrity.py -v
```

The tests include known CRC vectors, independent bitwise checks, malformed
headers, payload bit flips, missing frames, truncated streams, profile errors,
read bounds and counter handling. Local HTTP fixtures exercise the actual
decoder. Without PyAV, those end-to-end tests are explicitly skipped.

## First live result — 12 September 2026

The 30-second run checked **1,292 FLAC frames**, decoding **1,323,008 stereo
sample frames** at 44.1 kHz. CRC-8, CRC-16, frame-sequence and decoding errors
were all zero. No observed device loss counter increased. The report is
`output/diagnostics/heltec-integrity-2026-09-12.json`.
