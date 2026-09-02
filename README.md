# PYTHIA//

**Handheld Divination Terminal**
Delphi Systems, Oracle Products Division
Owner's Manual, firmware series 0

Thank you for choosing PYTHIA//. Your terminal has been calibrated at the factory to deliver answers with no bias toward any outcome, including the one you were hoping for. Please read this manual before consulting the oracle.

## 1. Overview

PYTHIA// is a single-purpose terminal for tabletop play. It rolls dice and it answers yes/no questions. It does nothing else, and it does those two things with hardware-grade randomness that no polyhedral die can match.

The unit ships as a Waveshare ESP32-S3-Knob-Touch-LCD-1.8: a 360x360 round touch display set into a rotary knob, driven by an ESP32-S3 with 16 MB of flash and 8 MB of PSRAM. Rotation and touch are the only controls, and the only ones you will need.

## 2. Operation

On power-up the terminal runs its self-test. After about five seconds the terminal returns to the die it was last set to, armed and waiting for a touch. A new unit arms the oracle. Input during the self-test is discarded.

1. **Turn the knob** to browse the die list. A tick ring around the rim tracks your position.
2. **Hold still for one second.** The selection is confirmed and the display fades to black. The terminal is now armed.
3. **Touch the screen** to roll. A touch while the list is still showing takes the highlighted die and rolls it at once. The result stays on screen until the next touch, which rolls again.
4. **Turn the knob** at any time to return to the list.

## 3. Examples

The animations below are rendered by the host preview from the same code the terminal runs, so they show what the panel draws.

The self-test:

![Power-on self-test](resources/boot.gif)

One full turn of the knob. The tick ring marks the position in the list, and the highlight returns to where it started after ten detents:

![Die list](resources/menu.gif)

A D100 roll. The result arrives from the left and stays until the next touch, with the die name held at the rim:

![D100 roll](resources/d100.gif)

An oracle consultation answering YES, AND. The answer lands centred, and only after a fixed pause does the modifier arrive from the right, so nothing before it reveals whether one is coming:

![Oracle reveal](resources/reveal.gif)

## 4. Divination modes

| Mode | Result |
|---|---|
| D2, D4, D6, D8, D10, D12, D20, D100 | One face, 1 through N |
| D66 | Two independent d6, read as tens and units |
| ORACLE | YES or NO, with an optional AND or BUT |

## 5. Entropy assurance

Delphi Systems does not use pseudo-random sequences in any oracle product.

Every roll draws from the ESP32-S3 hardware random number generator through `esp_random()`. The generator mixes physical noise from the SAR ADC, from the radio when it is running, and from jitter between the free-running 20 MHz RC oscillator and the bus clock. The ADC source is enabled at startup and kept on, so the generator has thermal noise behind it whatever else the firmware is doing. In that state its output passes the NIST SP 800-22 statistical suite.

Each 32-bit draw is XORed with the CPU cycle counter, read at roll time. A roll happens in response to a touch, so the counter carries the moment of the tap at 240 MHz resolution, and the result depends on the operator as well as the silicon. XOR with an independent value preserves the uniformity of the hardware draw.

Faces are chosen by rejection sampling: a draw that falls in the incomplete final window of `2^32 mod sides` is discarded and taken again, so every face of every die is exactly equally likely. D66 rolls two independent d6, and the oracle rolls one d6 against its six outcomes.

## 6. Field service

The firmware is an Arduino project under PlatformIO. Connect the unit by USB-C and run:

```bash
make firmware                    # fetch dependencies, then build
make upload PORT=/dev/ttyACM1
make monitor
```

No third-party source is kept in this repository. `make deps` fetches the ST77916 panel driver and the font rasteriser, each pinned to a version and checked against a recorded hash, and `make firmware` runs it for you.

**CAUTION.** The USB-C port on this chassis reaches one of two processors depending on cable orientation. If the host enumerates a CH340 serial adapter on `/dev/ttyUSB*`, you are connected to the audio co-processor. Unplug the cable, flip it, and reconnect. PYTHIA// lives on the ESP32-S3, which enumerates on `/dev/ttyACM*`.

The oracle reveal can be previewed on a workstation without the unit attached:

```bash
make boot                                          # boot.gif
make menu                                          # menu.gif
make reveal                                        # reveal.gif
make reveal ANSWER=NO MODIFIER=- THEME=parchment
make reveal ANSWER=87 MODIFIER=- CAPTION=D100
```

What it renders is what the panel draws, down to RGB565 quantisation. Each target writes to the repository root by default; the copies in section 3 were written straight into `resources/` by passing `BOOT_GIF`, `MENU_GIF` or `REVEAL_GIF`.

## 7. Notices

PYTHIA// is a decision aid. Delphi Systems accepts no liability for characters lost, campaigns derailed, or plans abandoned on the strength of a BUT.
