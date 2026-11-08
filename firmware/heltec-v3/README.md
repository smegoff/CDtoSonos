# Heltec V3 firmware

This ESP-IDF firmware captures the NAD C541i's buffered PCM bus and provides a
complete lossless CD-to-Sonos appliance without a Raspberry Pi.

## Signal map

| Signal | NAD U302 | Wire | Buffer input | Buffer output | Heltec |
|---|---:|---|---|---|---|
| LRCLK | pin 1 | White | pin 3, A2 via 1 kΩ | pin 17, B2 | GPIO 5 |
| Audio data | pin 2 | Yellow | pin 4, A3 via 1 kΩ | pin 16, B3 | GPIO 6 |
| Bit clock | pin 3 | Blue | pin 2, A1 via 1 kΩ | pin 18, B1 | GPIO 4 |
| Digital ground | pin 7 | Black | pin 10/common ground | — | GND |

The complete installation guide and diagrams are in
[`docs/hardware`](../../docs/hardware/README.md).

## LoRa safety invariant

Before Wi-Fi, I2S or any service starts:

- GPIO 12 holds SX1262 reset low.
- GPIO 8 holds chip select high.
- LoRa SPI clock and MOSI are held low.
- MISO, BUSY and DIO1 are passive inputs.
- `CDTS_LORA_ENABLED=0` is enforced at compile time.
- No LoRa or RadioLib component is linked.

## Runtime behaviour

- Receives native stereo, 16-bit right-justified PCM in 24-bit slots.
- Serves continuous stereo `audio/flac` on port 8000 at 44.1 kHz and 16 bits.
- Uses lightweight fixed-predictor/Rice FLAC coding. Flat bypass sends
  the captured PCM unchanged; enabled EQ shapes the PCM before encoding.
- Discovers Sonos topology after Wi-Fi connects.
- Saves the selected coordinator UUID/IP/name in NVS.
- Starts after 1.5 seconds of sustained audio.
- Holds through silence for 90 seconds to cover track changes.
- Tracks the selected Sonos coordinator separately from browser listeners.
- Reopens a stalled or heavily skipping Sonos stream while fresh CD audio
  is present. The dashboard counts recovery attempts.
- Uses stream-side EQ and leaves Sonos room settings intact.

### Graphic EQ and connection recovery

The dashboard offers ten bands from 31.5 Hz to 16 kHz, each adjustable from
−6 to +6 dB. Choose **Flat**, **Warm**, **Bass Boost**, **Vocal**, **Bright**,
or **Custom**, then click **Apply EQ**. The green segments show boost or cut
from the 0 dB centre. Flat is the power-on default. Changes crossfade over
one PCM block, and enabled curves reserve headroom and count clipped samples.
Sonos and local browser playback receive the same stream.

The Heltec tries the strongest compatible access point for the saved network
when connecting. The dashboard reports its BSSID, channel and reconnect count.
Sonos recovery uses the same reopen sequence as **Play** if the selected
player goes five seconds without a completed frame send, or skips roughly a
second of audio within five seconds. Recovery attempts are spaced by at least
30 seconds. These triggers measure stream delivery; audible distortion alone
may not register as a fault.

The smoked-glass EQ build was flashed and checked with **Warm** active on
8 November 2026. In a 30-second run, 44.1 kHz capture and Sonos delivery kept
pace with zero new overruns, clipped samples, capture losses or clock events.

## Build

### Capture diagnostics (0.1.4)

The I2S read timeout is expressed in milliseconds. Version 0.1.3 incorrectly
converted it to FreeRTOS ticks first, creating short waits and misleading empty
read counters. An empty read alone does not prove a cable fault or lost audio.

The dashboard reports DMA receive-buffer loss, failed publication of captured
blocks, elapsed capture age, and capture interruptions separately. Clock loss
requires 1.2 seconds without received bytes; stale audio levels clear when this
happens. Software ring overruns still count consumers falling behind and are
separate from receive loss. All counters reset on reboot. Normal delivery is
approximately 44,100 stereo frames/s; LRCLK is a short-window estimate, not a
precision frequency or sample-integrity measurement.

### Responsive meters and LED spectrum (0.1.5)

The Live Audio panel has a vintage champagne-metal faceplate, recessed smoked
display and hardware-style switches. A custom retro SONOS wordmark is labelled
as a DIY, Sonos-compatible CD-S1 transport; this is not official Sonos hardware.
The theme uses embedded HTML/CSS, with no external fonts, images or services.

The dashboard uses a small `/api/meters` snapshot up to 10 times/second;
full system status is refreshed every two seconds. Only one telemetry request
is active at a time, requests time out, and hidden tabs poll every five seconds.
No additional FLAC listener is opened for the visualisations.

The low-priority analysis task computes short-block RMS approximately 43
times/second. Peaks retain five blocks (approximately 116 ms) so a brief
transient is not missed between display polls. VU-style needles use a labelled
dBFS scale with fast display ballistics; they are not calibrated mechanical
300 ms VU instruments. Separate 48-segment LED peak bars have immediate attack
and a bright held-peak cell, with green, amber and red level zones. Rectangular
lenses have bevelled highlights and a soft colour-matched glow. Two narrow
browser-only blur layers provide the light spill; the numeric readouts and
solid lens faces stay sharp. No additional audio analysis or polling is used.

The rack-style spectrum has 31 nominal third-octave bands, 20 Hz to 20 kHz,
inspired by the [RTA-31 demonstration](https://www.youtube.com/watch?v=CKvP4GjoLZc&t=173s).
Persistent bandpass filters measure left/right energy separately, avoiding
antiphase cancellation. A roughly 60 ms power envelope and roughly 10.8 Hz
publication feed browser-side LED animation and falling peak markers.
Filter coefficients follow the constant-peak-gain bandpass in the
[W3C Audio EQ Cookbook](https://www.w3.org/TR/audio-eq-cookbook/).
This is a music display, not a standards-calibrated measurement analyser;
constant-Q bandwidth departs from exact third-octave spacing near Nyquist.

Display sensitivity affects the drawing only. The separate graphic EQ changes
the streamed PCM when enabled; the sample rate stays at 44.1 kHz.
`analysis_dsp_us` / `analysis_dsp_max_us` measure analysis wall
time (including preemption), not CPU utilisation. Telemetry that stops advancing
fades out instead of leaving frozen live-looking levels.

The **Fullscreen** button expands only the Live Audio panel, retaining the
VU/spectrum selector, sensitivity and peak bars. Exit with the same button or
Escape. Browsers that deny the element Fullscreen API use a panel-only overlay
inside the browser window instead. Background controls are inert until exit;
focus and scroll position are restored. Fullscreen is browser-only: it adds no
stream client, telemetry loop or ESP32 audio work. The implementation follows
the [element Fullscreen API](https://developer.mozilla.org/en-US/docs/Web/API/Element/requestFullscreen).

Visual and health regression tests need only Node and Python:

```sh
node --test tests/test_heltec_visuals.cjs
node --test tests/test_heltec_fullscreen.cjs
python tests/test_heltec_health.py
```

The portable `firmware/heltec-v3/tests/spectrum_analyzer_test.cpp` can be
built with a host C++17 compiler. It covers tones, stereo/antiphase energy,
low-frequency stability, silence decay and reset. Run it from the repository
root, for example:

```sh
c++ -std=c++17 -O2 firmware/heltec-v3/tests/spectrum_analyzer_test.cpp -o spectrum-test
./spectrum-test
```

### Build commands

After changing a file under `main/web`, regenerate the embedded assets:

```sh
python tools/embed_web.py
```

```sh
pio run
pio run --target upload --upload-port COM13
```

The build is pinned by `platformio.ini` and `dependencies.lock`. Generated
`sdkconfig.*`, `.pio/`, and `managed_components/` directories are intentionally
excluded from Git.

Wi-Fi is provisioned through the setup access point and web dashboard; no SSID
or password is present in source code.
