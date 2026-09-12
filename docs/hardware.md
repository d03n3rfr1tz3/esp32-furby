# Hardware — pin map, wiring and measurements

The physical counterpart to [FSD.md](FSD.md). It holds what is true about *this* device: which
part is installed, which GPIO carries which signal, and the numbers measured on the bench.
Whenever the FSD says "to be measured", the number belongs here.

**Two kinds of statement live here, and they are never mixed.** Rows marked *plan* come from
[`SmartFurby.fzz`](SmartFurby.fzz), the Fritzing design kept alongside this document — they are
what was intended, read off its netlist, and have not been checked against the assembled device.
Rows marked *measured* come from the bench. A cell that is neither reads `—`.

---

## 1. Installed parts

| Part | What is installed | Source |
| --- | --- | --- |
| ESP32 module | HiLetgo ESP-WROOM-32 devkit | plan |
| Motor driver | **Adafruit TB6612** breakout, channel A only | plan |
| Motor | Original Furby gearmotor, on MOTORA1/A2 | plan |
| Eye LEDs | Two red 5 mm LEDs in series, switched by Q2 | plan |
| Light sensor | Original LDR, divider against R5 = 68 kΩ | plan |
| Cam encoder | IR LED (940 nm) + LTR-301 phototransistor | plan |
| IR head pair | IR LED (940 nm) via Q1 + 3-pin receiver module | plan |
| Charger | TP4056 | plan |
| Boost converter | MT3608, Li-ion → the 5 V rail | plan |
| Battery | Single Li-ion cell | plan |
| Qi receiver | Coil into the TP4056 input; 5 W or 15 W unrecorded | plan |
| Speaker | Original transducer, currently on the DFPlayer's SPK1/SPK2 | plan |
| Microphone | Not in the plan at all — [HW-D2](FSD.md#hw-d2--microphone) | — |

Dropped: the **DFPlayer Mini**, which the plan still contains. All speech is live Home Assistant
TTS ([ARCH-D1](FSD.md#51-arch-d1--firmware-framework)), so it goes, and with it GPIO 5, 16 and 17.

## 2. Pin map

Every row *plan*, from the Fritzing netlist. Nothing here has been verified with a meter.

| Signal | GPIO | Mode | Wiring |
| --- | --- | --- | --- |
| Eye LEDs | 2 | output | Q2 gate; R8 = 10 kΩ pull-down. 3V3 → R7 22 Ω → two red LEDs in series → Q2 → GND |
| Motor AIN1 | 12 | output | TB6612 direction |
| Motor PWM | 13 | output (LEDC) | TB6612 PWMA |
| Motor AIN2 | 14 | output | TB6612 direction |
| Cam encoder | 18 | input, **pull-down** | LTR-301 phototransistor from 3V3; emitter IR LED is always on via R1 = 100 Ω |
| IR receive | 19 | input | 3-pin receiver module, own output stage |
| Tummy switch | 25 | input, pull-up | Switch to GND |
| Tongue switch | 26 | input, pull-up | Switch to GND |
| IR transmit | 27 | output | Q1 gate; R3 = 10 kΩ pull-down. 3V3 → R2 47 Ω → IR LED → Q1 → GND |
| Sync switch | 32 | input, pull-up | "Motor Startpunkt", switch to GND |
| Back switch | 33 | input, pull-up | Switch to GND |
| Light sensor | 34 | input only, ADC1 | LDR from 3V3, R5 = 68 kΩ to GND |
| Battery sense | 35 | input only, ADC1 | Divider, Bat R2 = 220 kΩ / Bat R1 = 100 kΩ — **but see §5** |

**GPIO 18 takes a pull-down, not the pull-up the legacy sketch used.** The phototransistor feeds
the pin from 3V3 with no emitter resistor, so the pin needs to be pulled *down* to have a low
state at all. The legacy value was never exercised — the cam sensor was only ever a stub — so it
is wrong rather than outdated. Confirm the phototransistor's orientation on the bench before
trusting the direction.

The four switches all sit on pull-up-capable pins, which GPIO 34–39 are not. That is not an
accident worth undoing.

### Free GPIOs

| Pins | Note |
| --- | --- |
| 4, 21, 22, 23 | Unrestricted, output-capable |
| 5, 16, 17 | Freed by dropping the DFPlayer. 5 is a strapping pin but usable |
| 15 | Strapping pin; pulling it low at boot silences the boot log. Reserve |
| 36, 39 | **Input only**, ADC1 — analogue or interrupt inputs, never outputs |
| 0, 1, 3 | Boot button and the UART0 console. Leave alone |
| 6–11 | Integrated SPI flash. Unusable |

Six unrestricted output-capable pins, seven counting GPIO 5, plus two input-only. What still
wants pins: an I²S microphone (3) sharing BCLK and WS with an I²S amplifier (1 more), the tilt
switch (1), the dock/charge signal (1), and a second eye channel if
[HW-D9](FSD.md#hw-d9--a-second-eye-colour-channel) goes ahead (1). That is seven against seven,
with GPIO 15 and the two input-only pins in reserve — and it loosens considerably if
[HW-D2](FSD.md#hw-d2--microphone) lands on an analogue electret, which costs one ADC1 pin instead
of three.

## 3. Power tree

```
Qi coil ─→ TP4056 ─┬─→ Li-ion cell
                   └─→ MT3608 (boost) ─→ Power switch ─→ 5 V rail
                                                          ├─ ESP32 VIN
                                                          ├─ TB6612 Vmotor + PWRIN
                                                          └─ (DFPlayer VCC — dropped)
```

The ESP32's own regulator makes 3V3, which feeds the TB6612's logic side, both IR parts, the
light sensor and the eye LED chain.

## 4. What the plan does not have yet

Each of these is a requirement the FSD already states, with nothing in the design answering it.

| Missing | Required by |
| --- | --- |
| **Tilt switch** — no such part in the plan | [F-20](FSD.md#f-20--switch-inputs), and §3.2 lists it as reused |
| **Dock / charge state signal** — the TP4056's status outputs go nowhere | [HW-D5](FSD.md#hw-d5--power), which calls it required, not optional; [F-42](FSD.md#f-42--voice-assistant-pipeline) and [F-45](FSD.md#f-45--volume-and-quiet-hours) both branch on it |
| **Separate motor rail with ≥ 470 µF at the driver** — Vmotor shares one net with the ESP32's VIN, and the design contains no capacitor at all | [HW-D5](FSD.md#hw-d5--power), "mandatory, from day one" |
| **Microphone and amplifier** — neither exists in the plan; the speaker still hangs on the DFPlayer | [HW-D2](FSD.md#hw-d2--microphone), [HW-D3](FSD.md#hw-d3--speaker-and-amplifier) |

## 5. Defects found in the plan

Two things read as drawing slips rather than intent. Both are cheap to fix and expensive to
discover later.

**The battery divider does not divide.** As drawn, Bat R1 (100 kΩ) sits directly across the cell
and Bat R2 (220 kΩ) runs from the positive terminal to GPIO 35, which has no path to ground. The
pin would sit near cell potential — up to 4.2 V, outside the ADC range and above what the pin
should see. The values give the intent away: 220 k over 100 k turns 4.2 V into 1.31 V, exactly
the divider that was meant. The fix is one wire — Bat R1's upper end belongs on GPIO 35, not on
the positive terminal.

**The TB6612's STBY pin is unconnected.** It sits alone on its breadboard column. The driver
stays in standby while STBY is low, so the motor would never turn. Either the breakout pulls it
high itself — check the board, Adafruit's does not necessarily — or it needs a deliberate tie to
3V3, or a GPIO if software wants to disable the driver. A GPIO is the better answer: it gives
[F-72](FSD.md#f-72--motor-safety-limits) a hard cut that does not depend on the PWM pin behaving.

Channel B of the TB6612 is entirely unused — a second motor output, free, if one ever helps.

## 6. Measurements

The bench prototype of [FSD §8.1](FSD.md#81-the-bench-prototype-m0) fills this section in. Until
it does, the decisions that depend on these rows stay open.

### 6.1 Motor and power ([HW-D5](FSD.md#hw-d5--power))

| Quantity | Value | Conditions |
| --- | --- | --- |
| No-load current | — | At the A2 running duty (150/255 @ 12 kHz) |
| Running current, gearbox loaded | — | |
| Stall current | — | |
| Inrush peak | — | Motor start with WiFi transmitting |
| Idle draw, WiFi active, motor off | — | |
| Usable PWM duty range | — | Lowest duty that reliably turns the gearbox, and the highest that is safe |
| Qi receiver profile | — | 5 W (BPP) or 15 W (EPP) |
| Battery temperature, continuous charge | — | Inside the closed enclosure — the ageing risk in HW-D5 |

### 6.2 Camshaft ([F-10](FSD.md#f-10--motor-control-with-homing))

| Quantity | Value | Notes |
| --- | --- | --- |
| Encoder edges per revolution | — | FSD §3.1 expects ≈ 416; confirm |
| Sync switch repeatability | — | Spread of the zero point over ten homing runs |
| Coast overshoot at running duty | — | Decides braking versus coasting in F-10 |
| GPIO 18 pull direction | — | Confirms §2's reading of the phototransistor |

### 6.3 Audio ([HW-D2](FSD.md#hw-d2--microphone), [HW-D3](FSD.md#hw-d3--speaker-and-amplifier))

| Quantity | Value | Conditions |
| --- | --- | --- |
| Electret level, close-talk | — | dBFS through the bench preamp |
| Electret level, ~1 m | — | dBFS, same gain |
| MEMS reference level, ~1 m | — | Same rig, for comparison |
| Speaker DC resistance | — | Infers impedance |
| TTS intelligibility, original speaker | — | Judgement, not a number: understandable or not |

### 6.4 Eyes ([HW-D8](FSD.md#hw-d8--eye-leds))

| Quantity | Value | Notes |
| --- | --- | --- |
| PWM dimming usable? | — | Smooth, and free of visible flicker |

## 7. Cam position table

The named poses of [F-11](FSD.md#f-11--named-poses-and-animation-sequencer) are angles in encoder
steps, measured against the real gearbox during M2. One row per pose, filled in empirically.

| Pose | Step | Notes |
| --- | --- | --- |
| `home` | — | New-in-box position: eyes and mouth open, ears up |
| `sleep` | — | |

## 8. Head space

[HW-D9](FSD.md#hw-d9--a-second-eye-colour-channel) needed two answers. The pin question is
settled in §2: GPIOs are available. What remains is physical.

| Question | Answer |
| --- | --- |
| Space left in the head, alongside the IR pair, the light sensor and the microphone | — |
| Is a second LED reachable without disassembling the head further? | — |
