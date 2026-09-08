# Hardware — pin map, wiring and measurements

The physical counterpart to [FSD.md](FSD.md). It holds what is true about *this* device:
which part is installed, which GPIO carries which signal, and the numbers measured on the
bench. Whenever the FSD says "to be measured", the number belongs here.

**Nothing in this document is an estimate.** A cell that has not been measured or read off the
board reads `—`. Estimates live in the FSD, against the decision they inform.

---

## 1. Installed parts

| Part | What is installed | Source |
| --- | --- | --- |
| ESP32 module | — | HW-D1 selects an ESP32 classic, WROOM-class; record the exact module and its flash size |
| Motor driver | — | HW-D4 identification task: read the part number off the board |
| Driver standby/enable pin | — | Tied high on the breakout, or on a GPIO? |
| Microphone | — | HW-D2 |
| Speaker | — | Original transducer; record diameter and measured DC resistance |
| Amplifier | — | HW-D3 |
| Eye LEDs | — | HW-D8: addressable (WS2812) or discrete? Read from the existing wiring |
| Qi receiver | — | HW-D5: 5 W (BPP) or 15 W (EPP)? |
| Battery | — | Chemistry, capacity, and the charger's termination behaviour |

## 2. Pin map

Carried over from the legacy sketch (FSD Appendix A1) and **not yet verified against the
rebuilt device**. Every row needs confirming with a meter before firmware drives it.

| Signal | GPIO | Verified | Notes |
| --- | --- | --- | --- |
| Status LED | 2 | no | On-board LED |
| Light sensor (LDR) | 34 | no | Input-only, ADC1 — mandatory, ADC2 is dead while WiFi is up (HW-D1) |
| Cam encoder | 18 | no | `INPUT_PULLUP`; optical interrupter |
| IR receive | 19 | no | `INPUT_PULLUP` |
| IR transmit | 27 | no | |
| Motor PWM | 13 | no | LEDC |
| Motor forward | 12 | no | |
| Motor backward | 14 | no | |

Not in the legacy map and still to be assigned:

| Signal | GPIO | Notes |
| --- | --- | --- |
| Sync switch | — | Confirmed wired; the legacy sketch never used it |
| Tummy switch | — | Also the push-to-talk button (HW-D7) |
| Back switch | — | |
| Tongue switch | — | |
| Tilt switch | — | |
| Eye LEDs | — | 1 pin if addressable, 2–4 if discrete (HW-D8) |
| Microphone | — | 1 / 2 / 3 pins depending on HW-D2 |
| Speaker / amplifier | — | 1 / 3 pins depending on HW-D3 |
| Dock / charge state | — | Required, not optional (HW-D5) |

Freed by dropping the DFPlayer: GPIO 16 and 17.

## 3. Measurements

The bench prototype of FSD §8.1 exists to fill this section in. Until it does, the decisions
that depend on these rows stay open.

### 3.1 Motor and power (HW-D4, HW-D5)

| Quantity | Value | Conditions |
| --- | --- | --- |
| No-load current | — | At the A2 running duty (150/255 @ 12 kHz) |
| Running current, gearbox loaded | — | |
| Stall current | — | |
| Inrush peak | — | Motor start with WiFi transmitting |
| Idle draw, WiFi active, motor off | — | |
| Usable PWM duty range | — | Lowest duty that reliably turns the gearbox, and the highest that is safe |
| Battery temperature, continuous charge | — | Inside the closed enclosure — the ageing risk in HW-D5 |

### 3.2 Camshaft (F-10)

| Quantity | Value | Notes |
| --- | --- | --- |
| Encoder edges per revolution | — | FSD §3.1 expects ≈ 416; confirm |
| Sync switch repeatability | — | Spread of the zero point over ten homing runs |
| Coast overshoot at running duty | — | Decides braking versus coasting in F-10 |

### 3.3 Audio (HW-D2, HW-D3)

| Quantity | Value | Conditions |
| --- | --- | --- |
| Electret level, close-talk | — | dBFS through the bench preamp |
| Electret level, ~1 m | — | dBFS, same gain |
| MEMS reference level, ~1 m | — | Same rig, for comparison |
| Speaker DC resistance | — | Infers impedance |
| TTS intelligibility, original speaker | — | Judgement, not a number: understandable or not |

## 4. Cam position table

The named poses of F-11 are angles in encoder steps, measured against the real gearbox during
M2. One row per pose, filled in empirically.

| Pose | Step | Notes |
| --- | --- | --- |
| `home` | — | New-in-box position: eyes and mouth open, ears up |
| `sleep` | — | |

## 5. Wiring notes

—
