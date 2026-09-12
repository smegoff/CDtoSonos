# Heltec responsive meters and fullscreen — 12 September 2026

## Scope

Firmware 0.1.5 adds short-block RMS/peak telemetry, a 31-band stereo-energy
spectrum and browser-only animation. A subsequent UI-only rebuild of 0.1.5
adds fullscreen for the Live Audio panel, followed by 48-segment LED L/R peak
bars. These builds do not change PCM gain,
format, capture priorities, the FLAC encoder, audio buffers or Sonos bonding.
LoRa remains disabled and held in reset.

## Live validation before the fullscreen-only rebuild

The Heltec at 192.168.10.161 was streaming a playing CD to the saved Office
Sonos coordinator, with the dashboard spectrum active. A read-only 180-second
health run produced 91 successful samples and no failed requests or resets.

| Observation | Result |
| --- | --- |
| Sonos stream clients | One throughout |
| Captured stereo PCM frames/s | 44,106.041 |
| Delivered stereo PCM frames/s | 44,100.350 |
| RMS/peak publications/s | 43.072 |
| Spectrum publications/s | 10.771 |
| New ring overruns / DMA losses / publication losses | 0 / 0 / 0 |
| New empty reads / capture interruptions | 0 / 0 |
| CPU usage, median / sampled range | 35.9% / 31.0–50.9% |
| Free heap, sampled range | 75,312–87,080 bytes |
| Capture age, sampled range | 0–22 ms |
| Latest analysis block wall time, sampled range | 4.511–12.978 ms |
| Lifetime maximum analysis block wall time at run end | 22.596 ms |
| Capture / analysis free stack | 3,916 / 2,820 bytes |

Rates use local monotonic request midpoints and include transport timing
uncertainty; these are not sample-clock precision measurements. Analysis timing
includes scheduler preemption, not just DSP execution. The audio block period
is approximately 23.22 ms. There was no captured-block loss during the run.

A separate 30-second FLAC decode test checked CRC/decode errors and observed
44.1 kHz, signed 16-bit stereo, 1,323,008 samples/channel and zero decoder
errors. This opened a second stream temporarily, after the one-client soak.
It does not prove the digital input matches the original disc bit for bit.
Sonos subsequently reported transport PLAYING / OK.

During later UI development, the lifetime ring-overrun count reached 35 by
approximately 20 minutes uptime, while capture/DMA/publication loss counters
remained zero. That occurred outside the clean 180-second run; its timing and
consumer were not isolated. Do not interpret this validation as a long-term
zero-dropout guarantee or proof of physical signal integrity. Intermittent
dashboard reconnects recovered automatically. Wi-Fi RSSI also varied.

## Fullscreen behaviour and regression checks

The existing Live Audio section is the native fullscreen target. It retains
the VU/spectrum toggle, display sensitivity and peak bars. Native exit and
Escape restore the dashboard. A rejected or unavailable native API uses a
fixed panel-only browser overlay with an exit button and keyboard focus trap.
Background controls become inert; their prior state, focus and scroll are
restored afterwards. No new audio client, telemetry loop or animation loop
is started by fullscreen.

Desktop browser inspection verified native fullscreen entry for both visuals,
mode switching, a reachable exit button and return to the dashboard. A
phone-sized fixture was used for layout exploration, but the browser permitted
native fullscreen out of its iframe; that is not an actual iPhone validation.
Its synthetic travelling-wave data was confined to localhost and never sent
to the Heltec or Sonos. The preview was closed after testing.

- 15 visual regression tests passed, including whole-cell LED quantisation,
  held-peak bounds and an unlit peak hold at silence.
- 12 fullscreen regression tests passed, including rejection, unsupported
  native API, Escape, Tab, focus/scroll/inert restoration, pending request
  cancellation, repeated entry and refused native exit.
- 9 health-sampler tests passed.
- The ESP-IDF/PlatformIO firmware build passed.
- Portable C++ spectrum tests are included, but were not run with a host C++
  compiler on this Windows machine. The spectrum header compiled in firmware;
  independent float32 numerical checks and the live DSP measurements provided
  supplementary checks, not a replacement for executing that native suite.

## Deployment and rollback

Only the verified active application region at 0x20000 was written on COM13;
esptool verified the programmed data. NVS, Wi-Fi and the saved Sonos target
were preserved. Sonos reconnected automatically after reboot.

The fullscreen-only rebuild passed a further 60-second one-client health
check: 31 successful samples, no resets or new capture/stream loss counters,
and approximately 44,106 captured/delivered frames per second. CPU samples
ranged from 34.6% to 46.5%. RSSI ranged from -83 to -66 dBm; that variability
remains relevant to longer-term network stability.

The final fullscreen + LED image is 919,040 bytes; static RAM is 150,016 bytes.
SHA-256: `B29592E33355737011E77115F72AB5A9FED7C33AF6799D23183D433E10ED4338`.
At 13 seconds after this final UI-only flash, the device had automatically
reconnected one Sonos client, clock/audio were present, all observed loss
counters were zero, CPU was 35.1%, and LoRa remained disabled/in reset.

Ignored local artifacts under `tmp/heltec-20260912/` retain the fullscreen
and LED binary/ELF images, earlier 0.1.5 and 0.1.4 rollback images and health reports. A full
pre-update flash backup is also retained privately; it includes credentials
and must not be published. No Git commit or GitHub push was made for this work.

### Rectangular LED glow follow-up

The next UI-only revision adds bevelled rectangular lenses and colour-matched
green/amber/red light spill. One small blurred layer per row sits outside the
segmented mask, with crisp solid lenses above it. Fill/glow use the same
whole-cell clipping; the 27 combined visual/fullscreen tests pass. Native
audio/DSP code is unchanged. The live dashboard appearance was inspected.

Application image: 920,160 bytes, SHA-256
`B53AAB754B5C229777C29F0393A9A4BB1E9856BAA7B4DA11406A9630CBDC269D`.
Saved as `tmp/heltec-20260912/firmware-0.1.5-glow.bin` with its ELF and previous
images retained. Esptool verified the programmed image on COM13.

At 13 seconds after reboot there was one Sonos client, captured audio was
present, and seven ring overruns had accumulated. At 58 seconds that count
was unchanged at seven, DMA/publication losses remained zero, the clock was
locked and CPU was 37.0%. This short startup check does not establish the
cause of those initial consumer overruns or long-term network stability.

The final LED-polish revision removes the normal telemetry explanation from
the panel, retaining only a status message while data is stale. Peak-hold
lenses and their glow now match their whole-cell colour zone (1–35 green,
36–43 amber, 44–48 red). The combined visual/fullscreen suite passes 28 tests.
Build artifact: `tmp/heltec-20260912/firmware-0.1.5-led-polish.bin`, SHA-256
`6A4757FC65B38E527CFF35C01F8A2DC085E88A72A7164C960536578DC69D6109`.

### Vintage monitor faceplate

Scoped the Live Audio panel to a brushed champagne faceplate with a dark
title strip, recessed smoked display, and metal-style mode/fullscreen switches
below the meters. The green/amber/red LED lenses, colour-matched peak hold,
VU/spectrum toggle and panel-only fullscreen remain. No equalizer was added.
Only HTML/CSS and their generated embedded asset changed in this revision;
capture, FLAC encoding, meter cadence and browser animation code are unchanged.

All 28 visual/fullscreen tests pass. Build static RAM remains 150,016 bytes.
The 924,944-byte application was programmed on COM13 with hash verification:
`tmp/heltec-20260912/firmware-0.1.5-vintage.bin`, SHA-256
`C331F4C516B2A240502E58FCB2E8E067453B44E0A9781FA029081C58094295A1`.
The preceding image and ELF are retained for rollback.

Live browser inspection covered spectrum, VU and native panel fullscreen.
At 110 seconds after reboot, one Sonos client was streaming, I2S was locked,
and stream overruns, DMA losses and publication drops were all zero. CPU was
43.0%, free heap 78,304 bytes and LoRa remained disabled. An earlier snapshot
during UI checks briefly showed 76.7% CPU; this is a short functional check,
not a long-duration performance certification or physical-phone validation.

The subsequent nameplate-only revision adds a custom retro SONOS wordmark,
CD-S1 model caption and a small DIY / Sonos-compatible label. This is styled
text, not a claim of official Sonos hardware. All 28 tests still pass, static
RAM is unchanged, and live desktop appearance was checked after flashing.
At 33 seconds, automatic playback had resumed with one client, locked clock,
zero overruns/DMA/publication losses and LoRa disabled.
Application: `tmp/heltec-20260912/firmware-0.1.5-nameplate.bin`, 925,680 bytes,
SHA-256 `99A5D2B93D1ACA3638C481C064FB1CBEDA19AEA962CAE98D3F68A9CD8DAE2DF7`.
