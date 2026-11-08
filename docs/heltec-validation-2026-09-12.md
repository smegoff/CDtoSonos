# Heltec short-cable validation — 12 September 2026

## Scope and outcome

Live-tested the existing NAD C541i / SN74LVC245 / Heltec V3 streamer after
the user shortened its signal cable to approximately 20 cm. The cable length
is user-reported, not independently measured. Sonos target was the saved
Office coordinator. No bonding, volume, network credentials or hardware wiring
was changed.

Firmware 0.1.4 was built and flashed on COM13. An 8 MiB flash backup was saved
first. The existing partition table and OTA selection were inspected: active
OTA sequence 1, valid state 2, application at 0x20000. Only that application
was written; esptool verified its hash. Wi-Fi and the Sonos preference survived.

## Implemented changes

- Correct the I2S read timeout to 150 milliseconds, without converting to
  FreeRTOS ticks first. ESP-IDF's `i2s_channel_read` argument is `timeout_ms`.
  The old conversion produced short waits and misleading timeout counts.
  Those counts were not proof of physical cable faults or 150 ms audio gaps.
- Determine capture loss by elapsed time without received bytes, clear stale
  meters on loss, and count loss transitions separately.
- Add DMA receive-queue loss, failed PCM publication, and last-block age
  diagnostics. Keep ISR work to a lock-free counter increment.
- Display those counters and capture delivery in the dashboard. Label empty
  I2S waits separately from actual lost buffers.
- Add a read-only health sampler with credential-filtered output and nine
  standalone unit tests.

Reference: [Espressif I2S API](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32s3/api-reference/peripherals/i2s.html).

No changes to PCM slot alignment, resampling, gain, FLAC encoding, task
priorities, or buffer sizes were made. LoRa's hardware reset remains asserted.

## Measurements

Before flashing, a 20.016-second live FLAC decode completed without decoder
errors or all-zero blocks. During a short status baseline, capture and stream
counters advanced and software-ring overruns remained zero; the misleading
empty-read counter continued increasing.

After flashing, the NAD initially sent digital silence while continuing to
clock the PCM bus. Audio subsequently resumed and automatic Sonos playback
started without pressing Play in the dashboard. Sonos GetTransportInfo
reported PLAYING / OK.

An independent post-update decoder check read 30.000 seconds of 44.1 kHz,
16-bit stereo FLAC: 1,323,008 samples per channel, 1,292 FLAC frames, zero
decoder errors and zero all-zero blocks. Peaks were -0.30 dBFS in both
channels; RMS was -15.03 / -15.99 dBFS. This test briefly added a second
listener and closed it afterwards.

Final single-Sonos sustained test, with the dashboard visible and both VU
and spectrum modes exercised:

| Measurement | Result |
| --- | --- |
| Duration | 179.976 seconds |
| Status requests | 91 successful; 0 failed |
| Capture delivery | 44,100.335 stereo PCM frames/s |
| Stream delivery, one listener | 44,100.335 stereo PCM frames/s |
| New software-ring losses | 0 |
| New DMA losses / publication drops | 0 / 0 |
| New capture-loss events / empty waits | 0 / 0 |
| Resets / listener-count changes | 0 / 0 |
| CPU, median observed reading | 26.3% across both cores |
| CPU observed range | 19.8–58.6%; core 0 briefly reached 100% |
| Free heap | 76,032–89,316 bytes |
| Wi-Fi RSSI | -65 to -58 dBm |
| Published block age | 0–22 ms |
| Longest status response | 219 ms |

CPU readings are short-window observations shared with dashboard polling,
not a precise time-weighted whole-run utilisation measure. Rates use the
sampler's monotonic request midpoints; short individual dashboard rate
readings naturally fluctuate with request and block timing.

## Remaining issue and limits

There was one early downstream stall in the first post-update observation
window: the software-ring counter increased by 62 blocks (approximately
1.44 seconds of PCM skipped by a reader). A status request at that time took
approximately 2.2 seconds and core 0 read 100%. Capture and analysis continued;
DMA loss, publication drops and capture-loss events stayed zero. This is
evidence of a delayed stream consumer, not evidence of damaged input samples.
The precise cause of that isolated delay was not established. It did not
repeat in the final three-minute test; the cumulative counter was still 62.

These results support stable delivery with the shorter cable in this test,
but are not a long-duration soak or an oscilloscope/sample-reference proof
of bit-perfect electrical capture. Clock-disconnect/recovery fault injection
was not performed. No further speculative timing or buffer changes were
applied. If downstream stalls recur, correlate reader losses and Wi-Fi
conditions first; keep the Wi-Fi antenna clear of the NAD's metal enclosure.

## Repeating the check

```powershell
python scripts/heltec_health.py http://cdtosonos.local --duration 180 --interval 2 --output tmp/heltec-health.json
python tests/test_heltec_health.py
```

The sampler does not start audio, open serial, reboot, or change settings.
The live reports and firmware backup from this session are under ignored
`tmp/heltec-20260912/`. Full flash backups include private saved configuration:
keep them private and never commit or publish them.

Firmware binary SHA-256:
`452f83cb92f10eaa6f4ebd9e89aeabda722ea9611014d1209c9f8affe13bfdf9`.
