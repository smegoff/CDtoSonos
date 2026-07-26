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
- Serves a continuous, bit-perfect `audio/flac` stream on port 8000.
- Uses lightweight fixed-predictor/Rice FLAC coding to reduce Wi-Fi traffic
  without changing any captured PCM sample.
- Discovers Sonos topology after Wi-Fi connects.
- Saves the selected coordinator UUID/IP/name in NVS.
- Starts after 1.5 seconds of sustained audio.
- Holds through silence for 90 seconds to cover track changes.
- Retries Sonos playback if the stream client disconnects.
- Never modifies Sonos grouping, bonding, surrounds, Trueplay, EQ or volume.

## Build

```sh
pio run
pio run --target upload --upload-port COM13
```

The build is pinned by `platformio.ini` and `dependencies.lock`. Generated
`sdkconfig.*`, `.pio/`, and `managed_components/` directories are intentionally
excluded from Git.

Wi-Fi is provisioned through the setup access point and web dashboard; no SSID
or password is present in source code.
