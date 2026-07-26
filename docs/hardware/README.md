# NAD C541i internal installation

This guide documents the hardware configuration that produced stable,
lossless playback in the working prototype. Read the safety section completely
before beginning.

## Documents

- [Electrical schematic](circuit-schematic.svg)
- [Suggested vero-board layout](vero-board-layout.svg)
- [Firmware signal map](../../firmware/heltec-v3/main/board.hpp)

## Parts

| Quantity | Part | Notes |
|---:|---|---|
| 1 | Heltec WiFi LoRa 32 V3 | ESP32-S3 board used by the firmware |
| 1 | SN74LVC245AN | DIP-20; use LVC, not HC/HCT |
| 3 | 1 kΩ resistor | Series protection for BCLK, LRCLK and DATA |
| 1 | 100 nF ceramic capacitor | Directly between buffer VCC and GND |
| 1 | DIP-20 socket | Strongly recommended |
| 1 | Stripboard/veroboard | 2.54 mm pitch, at least 18 × 14 holes |
| 2 | Header strips/sockets | For removable Heltec connections |
| — | 26–28 AWG insulated wire | Keep digital signal leads short |
| — | Heat-shrink and standoffs | Insulation and mechanical support |

The optional `EMPH` input is not required for audio capture and is omitted from
this verified build.

## NAD tap points

U302 is the Burr-Brown/TI PCM1732U DAC on the underside of the NAD main board.
Pin 1 is identified by the package dot.

| U302 pin | Signal | Wire colour used in prototype |
|---:|---|---|
| 1 | LRCIN / LRCLK | White |
| 2 | DIN / audio data | Yellow |
| 3 | BCKIN / bit clock | Blue |
| 7 | DGND | Black |

Use flexible, fine wire and strain-relieve it close to the tap. Do not allow
the soldered ends to move or load the surface-mount pins. Check with a
multimeter that pins 1, 2 and 3 are not shorted to each other or to ground
before applying power.

## Buffer wiring

The 74LVC245 is powered from the Heltec's 3.3 V rail. Its inputs tolerate the
NAD logic level while its outputs are correct for the ESP32-S3.

| 74LVC245 pin | Connection |
|---:|---|
| 1, DIR | Heltec 3.3 V, selects A → B |
| 2, A1 | NAD BCKIN through 1 kΩ |
| 3, A2 | NAD LRCIN through 1 kΩ |
| 4, A3 | NAD DIN through 1 kΩ |
| 5–9, A4–A8 | Ground |
| 10, GND | Common NAD/Heltec digital ground |
| 11–15, B8–B4 | Leave open |
| 16, B3 | Heltec GPIO 6, DATA |
| 17, B2 | Heltec GPIO 5, LRCLK |
| 18, B1 | Heltec GPIO 4, BCLK |
| 19, /OE | Ground, permanently enables outputs |
| 20, VCC | Heltec 3.3 V |

Fit the 100 nF capacitor between pins 20 and 19 at the IC socket. Keep it as
close to the pins as possible.

## Vero-board layout

The drawing uses 18 columns × 14 copper strips with the tracks running
horizontally. The DIP socket sits at columns 10 and 13.

Make these cuts on the copper side before fitting components:

1. Cut every IC row between the two pin columns: column 11 on rows 3–12
   (10 cuts).
2. Cut each input signal track at column 6 on rows 4, 5 and 6.
3. Inspect every cut with a magnifier and verify isolation with continuity mode.

The three 1 kΩ resistors bridge the row 4/5/6 input cuts. The cuts force the NAD
signals through the resistors rather than allowing the copper strip to bypass
them.

![Suggested vero-board layout](vero-board-layout.svg)

The drawing is a suggested compact daughterboard, not a drill template. Confirm
the actual strip direction, hole numbering and available space before cutting.
Mount the assembly on insulated standoffs with the copper side unable to touch
the NAD chassis or existing boards.

## Power

For the validated installation, continue powering the Heltec through USB.

Do not connect NAD `D+5V` to the Heltec and do not install a 7805 directly from
an unidentified rail. A permanent internal supply needs:

- a measured source rail under all CD-player operating states;
- current and thermal calculations;
- input/output decoupling and protection;
- confirmation that added load and switching noise do not affect the DAC;
- a fuse or other appropriate fault protection.

That supply should be designed and validated as a separate stage.

## Pre-power checklist

- NAD unplugged while continuity tests are performed.
- U302 pins 1, 2 and 3 are mutually isolated.
- No signal pin is shorted to DGND.
- 74LVC245 pin 20 to pin 10 is not shorted.
- 74LVC245 pins 1 and 20 have continuity to Heltec 3.3 V.
- 74LVC245 pins 10 and 19 have continuity to common ground.
- Unused A inputs are grounded; unused B outputs are open.
- No NAD 5 V or 12 V rail enters the daughterboard.
- Board and wires cannot interfere with the transport, tray, ribbon cables or
  ventilation.

## First power-up

1. Power the Heltec by USB with the NAD still off.
2. Confirm the dashboard starts and LoRa reports disabled.
3. Power the NAD with no disc and check for a locked 44.1 kHz source clock.
4. Play a disc and verify both channels on the VU meters.
5. Confirm automatic Sonos playback and zero buffer overruns.
6. Power-cycle both devices and repeat the test before closing the case.

## Mains warning

The player contains lethal voltages. Stay out of the mains/transformer-primary
area, unplug before touching internal wiring, and have the finished installation
inspected by a qualified technician if you are not experienced with mains
equipment.
