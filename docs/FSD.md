# Functional Specification Document — ESP32 Furby

**Project:** `esp32-furby` — a 1998 Furby refurbished with an ESP32 brain
**Status:** Draft 1 (initial, feature-complete specification)
**Last updated:** 2026-09-04

---

## 1. Purpose & Scope

### 1.1 What this document is

A living specification of *every* planned feature of the project. It exists so that each
working session can pick **exactly one feature** (or less), plan it in detail, implement it and
test it — without re-deriving the overall design each time.

Features are written **functionally**: what the Furby must do, and how we know it works. They
are deliberately **not** bound to one firmware framework, because that decision is still open
(see [ARCH-D1](#5-architecture-decision-arch-d1--framework)). Where the implementation path
differs meaningfully between the candidate frameworks, the feature carries a short
*Implementation note* for each path.

### 1.2 What is in scope

- Replacing the original Furby mainboard with an ESP32; driving the original motor, gearbox,
  sensors and switches from it.
- A **voice assistant** front-end for Home Assistant (wake word or push-to-talk → speech to
  text → intent → spoken answer).
- **Reactive phrases** on prepared smart-home events, in three personality modes.
- A useful subset of the original Furby's animatronic and reactive behaviour.

### 1.3 What is explicitly out of scope

| Not doing | Why |
| --- | --- |
| Needs/mood simulation (hunger, tiredness, attention) | Explicitly descoped; the Furby is a reactive character, not a tamagotchi. |
| Furbish → English learning stages | Charming but large; the assistant already speaks German. |
| Pre-rendered on-device voice samples | All speech is live TTS from Home Assistant. |
| Reviving the original Furby ROM or sound chip | The mainboard is removed. |
| Multi-Furby swarm behaviour | Only the IR protocol is implemented; behaviour is single-device. |

### 1.4 How this document is maintained

- Every feature has a stable ID (`F-nn`). IDs are never reused.
- Decisions get a stable ID (`ARCH-Dn`, `HW-Dn`) and are resolved in-place in
  [§13 Decision Log](#13-open-questions--decision-log), never deleted.
- When a feature is implemented, its status changes; the description stays as the reference of
  what was agreed.
- Language: this document and all repository content are **English**. The German phrases in
  [Appendix C](#appendix-c--phrase-catalogue) are content, not documentation, and stay German.

---

## 2. Background

### 2.1 The original project

An earlier attempt exists as an Arduino IDE sketch (`SmartFurby.ino` + `config.h`) built around:

- an ESP32 (classic) with hand-rolled WiFi/MQTT/OTA/NTP management,
- a **DFPlayer Mini** MP3 module playing pre-rendered samples from an SD card,
- motor control via LEDC PWM plus two direction pins,
- an IR receiver/transmitter pair with hand-captured raw Furby codes,
- an LDR on an ADC pin, and stubs for the cam position sensor.

It is **not continued**. The requirements moved on: speech should be live TTS driven by Home
Assistant, and the device should be a first-class voice assistant. What remains valuable — the
captured IR codes, the motor drive parameters, the quiet-hours logic and the pin map — is
preserved in [Appendix A](#appendix-a--salvaged-from-the-legacy-sketch).

### 2.2 The reference project

`d03n3rfr1tz3/TTGO.T-Watch.2020` already implements a complete Home Assistant voice assistant
on an ESP32 **without ESPHome**: a WebSocket client for the HA Assist pipeline, a recording
engine with DSP, a PDM microphone driver and an audio output layer. This proves the
custom-firmware path is viable and supplies directly reusable building blocks — see
[Appendix B](#appendix-b--reusable-from-the-t-watch-project).

### 2.3 Why a rewrite rather than a port

The old sketch and the new goal share almost no code: the audio subsystem is entirely
different, MQTT is replaced by the Home Assistant native path, and the behaviour engine did not
exist yet. Only the hardware knowledge transfers, and that is what the appendices capture.

---

## 3. Reference — the Furby (1998)

Understanding the original hardware matters because we are reusing its mechanics.

### 3.1 Mechanics

- **A single DC motor** drives *all* movement through a camshaft and gear train: eyelids, ears,
  mouth, and the forward/backward rocking of the body. The original achieves 300+ distinct
  combinations of ear / mouth / eye positions purely by stopping the camshaft at different
  angles.
- **Position sensing** is an LED plus phototransistor reading slots in one of the base gears
  (an optical interrupter). One full camshaft revolution corresponds to ≈ 208 illumination
  changes; counting every state change yields ≈ **416 steps per revolution**.
- **A separate sync switch** provides an absolute reference point, used to calibrate/home the
  camshaft. The original firmware spins clockwise until the sync switch triggers, then advances
  a known offset to reach a named position.
- Named reference positions include the **Sleeping position** and the **New-in-Box / home
  position** (eyes and mouth open, ears up).

> Consequence for us: the camshaft is a *single-axis, position-controlled* actuator. Every pose
> and every animation is "drive the cam to angle X" or "sweep between angles X and Y". This is
> the core abstraction of [F-10](#f-10--motor-control-with-homing) and
> [F-11](#f-11--named-poses-and-animation-sequencer).

### 3.2 Sensors and inputs

| Original sensor | Location | Reused? |
| --- | --- | --- |
| Light sensor (LDR) | Forehead | Yes — [F-21](#f-21--light-sensor) |
| Tilt / ball switch | Body | Yes — [F-20](#f-20--switch-inputs) |
| Tummy switch | Belly | Yes — [F-20](#f-20--switch-inputs) |
| Back switch (petting) | Back | Yes — [F-20](#f-20--switch-inputs) |
| Tongue switch (feeding) | Mouth | Yes — [F-20](#f-20--switch-inputs) |
| Microphone | Front | Yes, as the assistant's microphone — [HW-D2](#hw-d2--microphone) |
| IR emitter + receiver | Forehead, between the eyes | Yes — [F-30](#f-30--infrared-receive), [F-31](#f-31--infrared-transmit) |
| Cam position sensor (optical) | Gearbox | Yes — [F-10](#f-10--motor-control-with-homing) |
| Sync switch | Gearbox | Yes — [F-10](#f-10--motor-control-with-homing) |

### 3.3 The infrared protocol

The protocol is **fully reconstructed and verified** — see
[Appendix A3](#a3-the-infrared-protocol-decoded) for the derivation and the verification. In
short:

**Bit encoding.** Every bit is one or two IR pulses of the same length, distinguished by the gap
that follows:

- a **`1` bit** = pulse, short gap, pulse, short gap  *(two pulses)*
- a **`0` bit** = pulse, long gap  *(one pulse; the long gap is two short gaps in a row)*

**Frame.** A start bit (always `1`) followed by **8 data bits, least significant bit first**. The
data byte carries the message number `N` twice:

```
bit  7 6 5 4 | 3 2 1 0
     ~N      |   N        high nibble = N inverted (a checksum), low nibble = the message number
```

A frame is therefore fully determined by its message number — nothing has to be stored per
message. The transmitter repeats each frame **6 times** with roughly 83 ms between packets.

**Message numbers.** 1998 Furbys understand **16 messages, numbered 0–15**; later models (Furby
Babies, Shelby) add 16 more while remaining backward compatible. Of the 16, **14 were captured**
in the legacy `config.h`, and the remaining two — **#2 and #8** — have been reconstructed from
the encoding rule. Their raw timings are known; what they *mean* is not. See
[Appendix A3](#a3-the-infrared-protocol-decoded) for the full table.

> Consequence for [F-30](#f-30--infrared-receive) / [F-31](#f-31--infrared-transmit): the codec
> is a handful of lines of arithmetic, not a table of 32 raw arrays. Decoding also gets a free
> integrity check — a frame whose high nibble is not the inverse of its low nibble is noise, not
> a Furby.

### 3.4 Original behaviour — what we take and what we leave

| Original behaviour | Our stance |
| --- | --- |
| Reacts to petting, tilting, tummy press, feeding | **Take** — mapped to configurable reactions ([F-51](#f-51--sensor-reaction-mapping)) |
| Falls asleep in darkness, wakes on light | **Take** — [F-52](#f-52--idle-and-ambient-behaviour) |
| Idle chatter, yawning, blinking when left alone | **Take** — [F-52](#f-52--idle-and-ambient-behaviour) |
| Dancing, singing, joke telling | **Take** as canned animation+phrase routines, triggerable from Home Assistant |
| Furby-to-Furby IR chatter | **Take** — receive and transmit, surfaced as smart-home events |
| Furbish, and learning English over four stages | **Leave** — replaced by real speech |
| Hunger / sleepiness / attention state machine | **Leave** — explicitly descoped |
| Overfeeding, hiccups, sickness | **Leave** |

---

## 4. Guiding Principles

1. **Original parts before add-on modules.** If a part of the 1998 Furby can be kept, keep it.
   An added module needs a concrete reason — not merely "it's the usual way".
2. **ESPHome is preferred, not mandatory.** It buys us a lot for free, but never at the price of
   ripping out an original part that would otherwise work.
3. **Home Assistant owns language.** All text and all voices live in Home Assistant. The device
   holds *behaviour*, not a phrase database. This keeps phrases editable without a reflash.
4. **One feature per session.** Every feature must be independently testable on real hardware.
5. **The Furby must not be annoying.** Quiet hours, motor duty limits and volume scheduling are
   first-class requirements, not polish.
6. **Fail soft.** Losing WiFi or Home Assistant must degrade the Furby, not brick it. It should
   still respond physically to being touched.

---

## 5. Architecture Decision ARCH-D1 — Framework

**Status:** open. This is the first decision to make; several hardware decisions depend on it.

### 5.1 The core tension

The two candidate frameworks differ in exactly the dimension we care most about — *how many
original parts survive*:

| | **A: ESPHome** | **B: Custom firmware** (ESP-IDF / Arduino) | **C: Hybrid** (ESPHome + own external components) |
| --- | --- | --- | --- |
| Original **analogue electret microphone** | ❌ Not usable. `microphone/i2s_audio` documents `adc_type: internal` as *"no longer supported"*. | ✅ Usable on ESP32 classic via `I2S_MODE_ADC_BUILT_IN` (needs a preamp). | ❌ Same limitation as A for the stock component; would need a custom microphone component. |
| Original **speaker** via internal DAC | ❌ `speaker/i2s_audio` requires `dac_type: external`. | ✅ ESP8266Audio `AudioOutputI2S(..., INTERNAL_DAC)` on ESP32 classic. | ❌ Same as A unless we write the driver. |
| Original **speaker driver** (the transducer itself) | ✅ Keep it, drive it from an I²S amp. | ✅ Keep it, either path. | ✅ |
| On-device wake word | ✅ `micro_wake_word`, essentially free (needs PSRAM → ESP32-S3). | ⚠️ Would have to be integrated by hand, or replaced by push-to-talk / HA-side wake word. | ✅ |
| Voice assistant pipeline | ✅ `voice_assistant` component, fully maintained. | ⚠️ Own WebSocket client — **but already written** in the T-Watch project. | ✅ |
| Audio output pipeline (mixing, ducking, resampling) | ✅ `mixer` / `resampler` / `speaker` media player. | ⚠️ Hand-rolled; ESP8266Audio covers playback but not mixing/ducking. | ✅ |
| Motor control with encoder + sync switch | ⚠️ No stock component — needs a custom external component either way. | ✅ Plain C++. | ✅ Custom external component. |
| Furby IR codec | ⚠️ `remote_receiver`/`remote_transmitter` handle raw timings; the Furby codec needs custom code. | ✅ Plain C++ (legacy sketch already has a matcher). | ✅ |
| Home Assistant integration | ✅ Native API, entities for free. | ⚠️ Hand-rolled (WebSocket or MQTT). | ✅ |
| Ongoing maintenance | ✅ Low — upstream maintains the hard parts. | ❌ High — audio and networking are ours forever. | 🟡 Medium. |
| Reuse of the T-Watch code | ❌ Little. | ✅ High — assist client, DSP, mic driver, audio layer. | ❌ Little. |

### 5.2 Decision aids

Answer these, and the choice follows:

1. **Is keeping the original electret microphone a hard requirement, or a nice-to-have?**
   If hard → path B on an ESP32 classic. If nice-to-have → A or C are open.
2. **Does the original speaker sound acceptable at all?** Measure first
   ([HW-D3](#hw-d3--speaker-and-amplifier)). A 1998 toy speaker reproducing TTS may sound poor
   enough that the whole "keep the original audio path" argument collapses — in which case
   path A becomes clearly attractive.
3. **How much do we want on-device wake word?** It is the single biggest free win of ESPHome.
   Without it, waking the Furby means pressing its tummy — which is arguably *more* Furby-like.
4. **How much long-term maintenance appetite is there?** Path B means owning an audio stack.

### 5.3 Recommendation

**Prototype path B first, on an ESP32 classic**, because it is the only path that can keep both
original audio parts, and because the T-Watch project removes most of its risk. Treat
[HW-D3](#hw-d3--speaker-and-amplifier) (does the original speaker sound acceptable?) as the gate:
if the answer is no, an external amp and speaker are needed anyway, at which point path A on an
ESP32-S3 becomes the better trade and only the behaviour components have to be rewritten as
external components.

Everything in [§9 Feature Catalogue](#9-feature-catalogue) is written so that this decision can
be deferred to the end of milestone M0 without invalidating any feature description.

---

## 6. Target Architecture

### 6.1 Logical blocks

```
                    ┌──────────────────────── Home Assistant ────────────────────────┐
                    │  Assist pipeline (STT / intent / TTS)   Phrase catalogue        │
                    │  Voice profiles: normal / cute / evil   Event automations       │
                    └───────────────▲───────────────────────────────▲────────────────┘
                                    │ audio + events                │ speak() / events
    ┌───────────────────────────────┴───────────────────────────────┴────────────────┐
    │                                  ESP32 Furby                                    │
    │                                                                                 │
    │  ┌──────────┐  ┌──────────┐  ┌───────────┐  ┌──────────┐  ┌──────────────────┐  │
    │  │  Sense   │  │ Infrared │  │   Audio   │  │  Voice   │  │     Persona      │  │
    │  │ switches │  │  rx / tx │  │  in / out │  │ assistant│  │ mode + reactions │  │
    │  │  light   │  │          │  │           │  │  client  │  │                  │  │
    │  └────┬─────┘  └────┬─────┘  └─────┬─────┘  └────┬─────┘  └────────┬─────────┘  │
    │       └─────────────┴──────────────┴─────────────┴─────────────────┤            │
    │                                                                    ▼            │
    │                                            ┌───────────────────────────────┐    │
    │                                            │  Motion: cam position control │    │
    │                                            │  poses + animation sequencer  │    │
    │                                            └───────────────┬───────────────┘    │
    │                                                            ▼                    │
    │                                    motor · encoder · sync switch · eye LEDs     │
    └─────────────────────────────────────────────────────────────────────────────────┘
```

### 6.2 Responsibility split

| Concern | Device | Home Assistant |
| --- | --- | --- |
| Phrase text | — | ✅ owns the catalogue |
| Voice / mode rendering | Requests a mode | ✅ picks the TTS voice per mode |
| Wake word | Depends on [HW-D7](#hw-d7--wake-word-strategy) | Alternative host |
| Speech-to-text, intents | Streams audio | ✅ runs the pipeline |
| Motion, poses, animation | ✅ owns entirely | May request a named animation |
| Mode state (normal/cute/evil) | ✅ owns, persists across reboot | Can set and read it |
| Sensor reactions | ✅ owns the mapping | Can override / extend via events |
| Quiet hours, volume schedule | ✅ enforces | May configure the schedule |
| Event → phrase automations | Emits events | ✅ decides what to say |

**Rationale:** the device must stay useful and physically alive when Home Assistant is
unreachable — it can still move, blink and react to touch. It just goes quiet.

### 6.3 Module boundaries

The same module cut works for both framework paths; only the packaging differs.

| Module | Responsibility | ESPHome path | Custom path |
| --- | --- | --- | --- |
| `furby_motion` | Motor drive, encoder counting, homing, cam angle control, safety | External component | Firmware module |
| `furby_pose` | Named poses, animation sequencer, talk animation | External component | Firmware module |
| `furby_sense` | Switch debouncing, light sensor, derived events | Mostly stock `binary_sensor` / `adc` | Firmware module |
| `furby_ir` | Furby IR codec on top of raw timings | External component over `remote_*` | Firmware module |
| `furby_audio` | Microphone capture + DSP, speaker output, volume | Stock `microphone` / `speaker` / `media_player` | ESP8266Audio + own DSP |
| `furby_assist` | Voice assistant pipeline | Stock `voice_assistant` | Port of T-Watch `assist_*` |
| `furby_persona` | Mode state, reaction table, idle behaviour, speak() entry point | External component + automations | Firmware module |
| `furby_link` | Home Assistant transport | Native API | WebSocket / MQTT client |

---

## 7. Hardware Inventory & Open Decisions

### 7.1 Inventory

| Part | Origin | Notes |
| --- | --- | --- |
| DC motor + camshaft gearbox | Original | Single actuator for all movement |
| Optical cam encoder (LED + phototransistor) | Original | ≈ 416 counted steps per revolution |
| Sync switch | Original | Absolute home reference |
| Tummy, back, tongue switches | Original | Simple contacts |
| Tilt switch | Original | Ball/tilt contact |
| Light sensor (LDR) | Original | Forehead, analogue |
| Electret microphone | Original | Preamp was on the removed mainboard |
| Speaker | Original | Impedance and rating to be measured |
| IR LED + IR receiver | Original | Forehead |
| Eye LEDs | Added, already prepared | Must be able to go red for evil mode |
| ESP32 module | Added | See [HW-D1](#hw-d1--mcu-choice) |
| Motor driver | Added | See [HW-D4](#hw-d4--motor-driver) |
| Power supply | Added | See [HW-D5](#hw-d5--power) |

### 7.2 Open decisions

Each decision lists options, the trade-off, a recommendation, and — where relevant — a
**measurement task** to perform on the real Furby before deciding.

#### HW-D1 — MCU choice

| Option | Pros | Cons |
| --- | --- | --- |
| **ESP32 classic** (WROOM-32) | Has an internal DAC and ADC-over-I²S → the only chip that can keep both original audio parts. Matches the legacy wiring and pin map. Cheap, plentiful GPIOs. | No practical on-device wake word. Tight RAM for audio. Effectively forces framework path B. |
| **ESP32-S3** (WROOM-1, with PSRAM) | PSRAM makes `micro_wake_word` and the ESPHome audio pipeline comfortable. More RAM and CPU headroom overall. | **No DAC and no ADC-over-I²S** → digital microphone and I²S amplifier become mandatory, regardless of framework. |

**Coupling:** this decision is joined at the hip with [ARCH-D1](#5-architecture-decision-arch-d1--framework)
and [HW-D2](#hw-d2--microphone). Choosing the S3 forecloses reusing the original audio parts.

**Recommendation:** decide after [HW-D3](#hw-d3--speaker-and-amplifier) is measured. Default to
ESP32 classic if the original audio parts prove usable; otherwise ESP32-S3 with ≥ 8 MB PSRAM.

#### HW-D2 — Microphone

| Option | Pros | Cons |
| --- | --- | --- |
| **Original electret + small preamp** (MAX9814 / MAX4466) | Keeps an original part. Preamp boards are tiny. | ESP32 classic **only**, custom firmware **only**. Noise floor of a 1998 electret is unknown. Needs a preamp because the original one is gone. |
| **PDM MEMS mic** (e.g. SPM1423, as in the T-Watch) | 2 pins only. Driver already written in the T-Watch project. Very small. | Original part removed. |
| **I²S MEMS mic** (INMP441 / ICS-43434) | Best noise performance. Works on every framework and both MCUs. | 3 pins. Original part removed. Board is ~15 × 18 mm. |

**Measurement task:** identify the electret's type and check whether it still works; record a
sample through a bench preamp and judge whether speech at ~1 m is usable.

**Recommendation:** attempt the original electret on the prototype; keep an INMP441 on the shelf
as the fallback. Note that the T-Watch's gain table reaching **+42 dB** shows that heavy gain
plus DSP is normal in this class of device — see [Appendix B](#b3-audio-dsp-parameters).

#### HW-D3 — Speaker and amplifier

| Option | Pros | Cons |
| --- | --- | --- |
| **Original speaker + internal DAC + small amp** (PAM8403 / LM4871) | Keeps the original transducer *and* the original signal path character. Frees three I²S pins. | ESP32 classic + custom firmware only. Internal DAC is 8-bit-ish quality. |
| **Original speaker + I²S amp** (MAX98357A) | Keeps the transducer; clean digital path; works on every framework and MCU. | One added module (~16 × 14 mm), 3 pins. |
| **New speaker + I²S amp** | Best audio quality. | Loses an original part; the Furby's speaker cavity constrains size anyway. |

**Measurement task (gates ARCH-D1):** measure the original speaker's DC resistance to infer
impedance, then drive it from a bench amplifier with a TTS sample. **Judge intelligibility, not
fidelity** — a Furby is allowed to sound like a toy, but the assistant's answers must be
understandable.

**Recommendation:** keep the original transducer in all cases. Prefer the **I²S amp** unless the
internal-DAC path is needed to justify staying on the ESP32 classic — the I²S amp keeps every
other option open.

#### HW-D4 — Motor driver

| Option | Notes |
| --- | --- |
| **DRV8833** | Small, 1.5 A per channel, built-in current limiting, fine for a toy gearmotor. Two PWM inputs (no separate enable). |
| **TB6612FNG** | Very common, PWM + two direction pins — matches the legacy wiring 1:1. ESPHome even ships a `grove_tb6612fng` component. |
| **Original H-bridge** | Was part of the removed mainboard. Not available. |

**Measurement task:** measure the motor's stall current and no-load current to size the driver
and the supply.

**Recommendation:** **TB6612FNG** — it matches the existing three-pin drive scheme
(PWM + forward + backward) from the legacy sketch, so [Appendix A](#a2-motor-drive-parameters)
transfers directly.

#### HW-D5 — Power

The motor's inrush current is the classic cause of ESP32 brownouts in this kind of build.

| Aspect | Options |
| --- | --- |
| Source | USB-C (simple, tethered) vs. Li-ion + charger (authentic, adds bulk and a low-battery phrase — the catalogue already has one) |
| Rail separation | A separate motor rail with generous bulk capacitance, or a shared rail with a low-ESR reservoir near the driver |
| Brownout | Must survive motor start while WiFi is transmitting |

**Measurement task:** motor stall and start currents (from HW-D4), plus the total idle draw with
WiFi active.

**Recommendation:** USB-C for the prototype (removes a whole variable), with the battery option
kept open in the enclosure plan. Separate motor supply rail with ≥ 470 µF bulk capacitance at
the driver from day one.

#### HW-D6 — GPIO budget

Rough pin count for the full feature set:

| Function | Pins |
| --- | --- |
| Motor (PWM + 2 direction) | 3 |
| Cam encoder | 1 |
| Sync switch | 1 |
| Light sensor (ADC) | 1 |
| Switches (tummy, back, tongue, tilt) | 4 |
| IR receive / transmit | 2 |
| Eye LEDs | 1–2 |
| Microphone | 1 (analogue) / 2 (PDM) / 3 (I²S) |
| Speaker | 1 (internal DAC) / 3 (I²S) |
| **Total** | **15–20** |

**Consequence:** very small boards (e.g. XIAO ESP32S3 at 21 × 17.5 mm) do **not** have enough
usable GPIOs. Either use a full WROOM module, or add an I²C port expander (PCF8574) for the four
slow switches, which brings the direct count down to ~13–18.

**Recommendation:** full WROOM-class module. Reserve the expander as the fallback if the
enclosure forces a smaller board.

#### HW-D7 — Wake word strategy

| Option | Pros | Cons |
| --- | --- | --- |
| **On-device** (`micro_wake_word`) | Hands-free, no streaming until woken, private. | Needs ESP32-S3 + PSRAM and, in practice, ESPHome. A custom German wake word ("Hey Förby") must be trained. |
| **Home Assistant side** | Works on any MCU/framework. | Requires continuous audio streaming — power, bandwidth and privacy cost. |
| **Push-to-talk on an original switch** | Zero extra cost, works everywhere, and *pressing the Furby's tummy to talk to it* is arguably the most Furby-like interaction of the three. | Not hands-free. |

**Recommendation:** implement **push-to-talk on the tummy switch first** (it is a prerequisite
for testing everything else anyway), and treat on-device wake word as an additive feature
([F-42](#f-42--voice-assistant-pipeline)) once the MCU is settled.

#### HW-D8 — Eye LEDs

| Option | Pros | Cons |
| --- | --- | --- |
| **WS2812 / addressable** | 1 pin for both eyes, full colour, effects for the assistant states. | Timing-sensitive; needs RMT. |
| **PWM red + white per eye** | Dead simple, no timing constraints. | 2–4 pins, limited palette. |

**Note:** the eye LEDs are already prepared in the build. This decision is really "what is
already wired in?" — confirm before designing [F-22](#f-22--eye-lighting).

**Recommendation:** WS2812 if the existing wiring allows, because the assistant states
(listening / thinking / speaking / error) benefit greatly from colour.

---

## 8. Milestones

| ID | Milestone | Contents | Exit criterion |
| --- | --- | --- | --- |
| **M0** | Decisions & measurements | HW-D1…HW-D8, ARCH-D1 | All decisions resolved and recorded in §13 |
| **M1** | Foundation | F-01, F-02 | Device boots, is reachable, can be updated over the air |
| **M2** | Motion | F-10, F-11, F-20, F-21, F-22 | The Furby homes, holds named poses, and reacts physically to touch |
| **M3** | Audio & voice | F-40, F-41, F-42, F-45 | A full voice interaction works end to end |
| **M4** | Personality | F-12, F-44, F-50, F-51, F-52 | Modes, talk animation and idle behaviour work together |
| **M5** | Smart home | F-60, F-61, F-62, F-63 | Prepared events produce the right phrase in the right mode |
| **M6** | Polish & robustness | F-30, F-31, F-70, F-71, F-72 | IR works; the device survives abuse and long uptimes |

IR (F-30/F-31) is deliberately late: it is delightful but not on the critical path.

---

## 9. Feature Catalogue

Each feature: **Goal** — one sentence. **Description** — what it does. **Acceptance** — how we
know it works. **Depends on** — prerequisites. Status is `planned` for all features in Draft 1.

### Foundation

#### F-01 — Base node

- **Goal:** a reliably reachable, updatable ESP32 that survives network trouble.
- **Description:** WiFi with two configured networks and automatic reconnection; NTP-backed
  local time with timezone; logging; over-the-air update; a recovery/safe mode; a watchdog. A
  connection loss must never leave the device wedged — the legacy sketch's escalating
  retry-then-restart strategy is the reference behaviour.
- **Acceptance:** device reconnects automatically after the access point is power-cycled;
  an OTA update succeeds; local time is correct after a cold boot; killing WiFi for 10 minutes
  and restoring it recovers without a manual reset.
- **Depends on:** ARCH-D1, HW-D1.
- **Milestone:** M1.

#### F-02 — Repository layout, build and validation

- **Goal:** anyone can build and check the firmware with one command.
- **Description:** a documented repository layout, a reproducible build (ESPHome YAML, or
  PlatformIO for the custom path), secrets kept out of version control, and a CI job that at
  minimum validates/compiles the configuration on every push.
- **Acceptance:** a clean checkout builds; CI fails on a deliberately broken config; no
  credentials are present in the repository.
- **Depends on:** ARCH-D1.
- **Milestone:** M1.

### Motion

#### F-10 — Motor control with homing

- **Goal:** drive the camshaft to any commanded angle, reliably and safely.
- **Description:** the core motion primitive. Drives the motor forwards/backwards under PWM,
  counts cam encoder edges, and uses the sync switch as the absolute reference. On boot (and on
  demand) the device **homes**: it drives in a defined direction until the sync switch triggers,
  zeroes the position counter, and records the steps-per-revolution actually observed. The
  commanded unit is a **cam angle** in encoder steps (0 … ~415).
  Safety is part of this feature, not an afterthought: a maximum continuous run time, a stall
  detection (motor commanded but no encoder edges within a timeout) that cuts drive and raises a
  fault, and a re-home after any fault or missed step.
- **Acceptance:** after homing, commanding the same angle ten times in a row lands within a
  defined tolerance every time; blocking the gearbox by hand raises a stall fault within the
  timeout and stops the motor; power-cycling and re-homing reproduces the same zero point.
- **Depends on:** F-01, HW-D4, HW-D5.
- **Implementation note (ESPHome):** custom external component; the encoder needs an interrupt-
  driven counter, so it cannot be built from stock components alone.
- **Implementation note (custom):** plain C++; the legacy sketch's LEDC parameters and the
  direction-change guard in [Appendix A](#a2-motor-drive-parameters) are the starting point.
- **Milestone:** M2.

#### F-11 — Named poses and animation sequencer

- **Goal:** turn raw cam angles into a vocabulary of expressions and movements.
- **Description:** a table mapping **named poses** (`sleep`, `home`, `eyes_open`, `eyes_closed`,
  `mouth_open`, `mouth_closed`, `ears_up`, `ears_down`, `lean_forward`, `lean_back`, …) to cam
  angles, calibrated once against the real gearbox. On top of it, an **animation sequencer**
  plays named sequences of poses with durations and repeat counts (`blink`, `nod`, `shake`,
  `yawn`, `dance`, `startle`, `sneeze`). Animations are interruptible and have priorities, so a
  reaction can pre-empt idle behaviour.
- **Acceptance:** each named pose visibly produces the expected face; `blink` looks like a blink
  at normal speed; starting a new animation mid-flight cleanly interrupts the previous one; the
  pose table is editable without touching code.
- **Depends on:** F-10.
- **Milestone:** M2.

#### F-12 — Talk animation

- **Goal:** the mouth moves while the Furby speaks.
- **Description:** while audio is playing, the mouth is animated. Two possible drivers, to be
  chosen during implementation: a simple oscillation for the duration of playback, or an
  envelope-follower on the outgoing audio so the mouth tracks loudness. Must respect the motor
  duty-cycle limits from [F-72](#f-72--motor-safety-limits) — a long answer must not run the
  motor continuously.
- **Acceptance:** a spoken sentence produces mouth movement that starts and stops with the
  audio; a 30-second answer does not exceed the motor duty limit; muting the speaker also stops
  the mouth.
- **Depends on:** F-11, F-40.
- **Milestone:** M4.

### Sensing

#### F-20 — Switch inputs

- **Goal:** the four original switches produce clean, debounced events.
- **Description:** tummy, back (petting), tongue (feeding) and tilt switches are read with
  debouncing. Beyond raw press/release, the layer derives higher-level events that the reaction
  table can use: short press, long press, repeated press, and — for the back switch —
  *stroking*, i.e. repeated actuation within a window.
- **Acceptance:** no phantom triggers over an hour of idling; a deliberate stroke of the back is
  distinguished from a single press; the tilt switch reports orientation changes reliably when
  the Furby is picked up and tipped.
- **Depends on:** F-01.
- **Milestone:** M2.

#### F-21 — Light sensor

- **Goal:** know whether it is bright or dark, and notice sudden changes.
- **Description:** the forehead LDR is sampled continuously and exposed as a smoothed level plus
  two derived events: *got significantly brighter* and *got significantly darker*. The legacy
  sketch's threshold — a change of more than 10 % within one second — is the starting point.
  A slow ambient drift over the day must **not** fire the events.
- **Acceptance:** covering the forehead with a hand fires "darker"; switching the room light on
  fires "brighter"; a sunset does not fire anything; the level reading is stable enough not to
  oscillate at a fixed light level.
- **Depends on:** F-01.
- **Milestone:** M2.

#### F-22 — Eye lighting

- **Goal:** the eyes light up, and can go red.
- **Description:** the eye LEDs are exposed as a controllable light with colour and brightness.
  Beyond manual control, the eyes convey state: assistant states (idle / listening / thinking /
  speaking / error) and mode (notably **red in evil mode**, per
  [F-50](#f-50--mode-manager)). Brightness must follow the quiet-hours schedule so the Furby
  does not glow at full power at night.
- **Acceptance:** eyes can be set to any colour from Home Assistant; evil mode turns them red
  and normal mode restores them; the assistant states are visually distinguishable across the
  room.
- **Depends on:** F-01, HW-D8.
- **Milestone:** M2.

### Infrared

#### F-30 — Infrared receive

- **Goal:** recognise Furby-to-Furby messages from another Furby.
- **Description:** raw IR timings are captured and **decoded**, not pattern-matched: classify each
  gap as short or long, assemble the start bit plus 8 data bits, and validate that the high
  nibble is the inverse of the low nibble ([§3.3](#33-the-infrared-protocol)). The result is a
  message number 0–15, surfaced as a named event. A failed checksum means "not a Furby" and is
  discarded. Only the short/long gap threshold needs tuning against the real receiver.
- **Acceptance:** a second Furby (or a replay of a captured code) is recognised by its message
  number and name; ordinary household remotes are rejected by the checksum rather than
  mis-recognised; all 16 numbers decode, including the two that were never captured.
- **Depends on:** F-01.
- **Milestone:** M6.

#### F-31 — Infrared transmit

- **Goal:** send Furby messages to another Furby.
- **Description:** transmit any of the 16 messages on the forehead IR LED, **generated from its
  number** rather than replayed from a stored array, repeated 6 times as the original does.
  Triggerable from Home Assistant and from the reaction table.
- **Acceptance:** a second Furby reacts to a transmitted greeting; our own receiver
  ([F-30](#f-30--infrared-receive)) recognises our own transmission in a loopback test; all 16
  numbers transmit, which is also the opportunity to discover what messages **#2 and #8** mean.
- **Depends on:** F-30 (shares the codec).
- **Milestone:** M6.

### Audio & Voice

#### F-40 — Audio output chain

- **Goal:** the Furby can play arbitrary audio through its speaker at a controlled volume.
- **Description:** the output path from a decoded audio stream to the speaker, including volume
  control with minimum/maximum limits, mute, and — if the framework supports it — ducking so a
  spoken answer can talk over other audio. Must be able to play a stream received from Home
  Assistant (a TTS response) as well as a locally generated tone for diagnostics.
- **Acceptance:** a TTS response plays intelligibly at a normal listening distance; volume
  changes take effect immediately; a diagnostic tone confirms the path without any network.
- **Depends on:** F-01, HW-D1, HW-D3.
- **Implementation note (ESPHome):** `speaker` + `mixer` + `resampler` + the `speaker` media
  player platform; note that `voice_assistant` accepts either a `speaker` **or** a
  `media_player`, never both.
- **Implementation note (custom):** ESP8266Audio with `AudioOutputI2S`, streaming the TTS
  response from RAM as in the T-Watch project ([Appendix B](#b4-microphone-and-speaker-drivers)).
- **Milestone:** M3.

#### F-41 — Microphone chain

- **Goal:** capture clean 16 kHz mono speech suitable for a speech-to-text pipeline.
- **Description:** microphone capture at **16 kHz, 16-bit, mono**, with the conditioning that
  makes a cheap microphone usable: a high-pass to remove rumble, gain, and a look-ahead limiter
  to stop clipping when someone speaks close to the toy. A level readout in dBFS is required for
  diagnostics and for tuning gain. The T-Watch parameters in
  [Appendix B](#b3-audio-dsp-parameters) are the starting values.
- **Acceptance:** speech at 1 m lands in a healthy level range without clipping; the motor
  running does not render speech unintelligible (or, if it does, this is documented and the
  motor is muted during listening); the level readout responds correctly to silence and to
  loud speech.
- **Depends on:** F-01, HW-D2.
- **Milestone:** M3.

#### F-42 — Voice assistant pipeline

- **Goal:** talk to the Furby and get a spoken answer from Home Assistant.
- **Description:** the full interaction: trigger → listen → stream audio → receive the
  transcript, the answer text, and the spoken answer → play it. Triggering is push-to-talk on
  the tummy switch initially ([HW-D7](#hw-d7--wake-word-strategy)); on-device wake word is an
  additive extension. The pipeline must handle errors visibly (see
  [F-44](#f-44--assistant-state-feedback)) and time out rather than hang. A conversation
  timeout resets the conversation context.
- **Acceptance:** press tummy → speak a command → Home Assistant executes it → the Furby speaks
  the answer; a network failure mid-request produces a visible error state and returns to idle;
  the device recovers without a reboot.
- **Depends on:** F-40, F-41, F-20.
- **Implementation note (ESPHome):** `voice_assistant` component; `voice_assistant.start` /
  `.stop` bound to the tummy switch; wake word via `micro_wake_word` if the MCU allows.
- **Implementation note (custom):** port the T-Watch assist client
  ([Appendix B](#b1-home-assistant-assist-client)) — it already covers auth, pipeline selection,
  audio framing, the run state machine and TTS retrieval.
- **Milestone:** M3.

#### F-43 — Mode-dependent voice

- **Goal:** the Furby's voice matches its mode.
- **Description:** normal, **cute** (high, childlike) and **evil** (low, sinister) are rendered
  by Home Assistant using different TTS voices or pitch settings. The device only communicates
  *which mode it is in*; the mapping from mode to voice lives in Home Assistant so it can be
  changed without a reflash.
- **Acceptance:** the same sentence spoken in all three modes is audibly distinct; switching
  mode changes the voice of the very next utterance.
- **Depends on:** F-42, F-50.
- **Milestone:** M4.

#### F-44 — Assistant state feedback

- **Goal:** you can see what the Furby is doing without listening.
- **Description:** the assistant's states drive the eyes and the body: *listening* (e.g. ears up,
  eyes wide, a distinct eye colour), *thinking*, *speaking* (mouth animation via
  [F-12](#f-12--talk-animation)), *error* (a visible, unmistakable signal). Feedback must
  degrade gracefully during quiet hours — visual only, no motor.
- **Acceptance:** an observer can tell listening from thinking from speaking across a room; an
  error is obvious; during quiet hours the same states are conveyed without motor noise.
- **Depends on:** F-22, F-11, F-42.
- **Milestone:** M4.

#### F-45 — Volume and quiet hours

- **Goal:** the Furby is loud enough by day and unobtrusive at night.
- **Description:** a time-of-day schedule controls speaking volume, whether the motor may run,
  and whether the Furby may speak unprompted at all. The legacy sketch's behaviour is the
  starting point: motor allowed 08:00–20:00, speech allowed 08:00–22:00, volume 75 % between
  09:00 and 19:00, 50 % at the shoulders and 25 % otherwise. Explicitly requested interactions
  (someone pressed the tummy and asked a question) must still work outside those windows, just
  quietly — the restriction applies to *unprompted* output.
- **Acceptance:** an event-triggered phrase at 23:00 does not play; a direct voice request at
  23:00 is answered quietly; volume steps correctly at the schedule boundaries; the schedule is
  configurable from Home Assistant.
- **Depends on:** F-01, F-40.
- **Milestone:** M3.

### Personality

#### F-50 — Mode manager

- **Goal:** the Furby has three personalities, and remembers which one it is in.
- **Description:** a mode of `normal`, `cute` or `evil`, settable from Home Assistant and from
  physical interaction, persisted across reboots. Changing mode has an immediate visible and
  audible effect: **evil mode turns the eyes red**, and the mode change is announced with the
  corresponding phrase from the catalogue (`002-Cute-Mode` / `002-Evil-Mode`). Mode influences
  the TTS voice ([F-43](#f-43--mode-dependent-voice)), the eye colour, the reaction table and
  the phrase variant chosen for an event.
- **Acceptance:** setting evil mode turns the eyes red, announces it, and the next phrase uses
  the evil voice; the mode survives a power cycle; every mode has a defined eye colour.
- **Depends on:** F-22, F-42.
- **Milestone:** M4.

#### F-51 — Sensor reaction mapping

- **Goal:** touching the Furby makes it react like a Furby.
- **Description:** a **configurable table** mapping an input event (tummy press, back stroke,
  tongue press, tilt, sudden light change, IR message received) to a reaction: an animation, a
  phrase request, an eye effect, or a combination. Entries are mode-dependent — evil mode reacts
  differently to being stroked than cute mode. The table must be editable without a firmware
  change; reactions have cooldowns so repeated poking does not produce a machine-gun of phrases.
- **Acceptance:** stroking the back produces a visible, mode-appropriate reaction; poking ten
  times in five seconds produces at most the configured number of reactions; changing an entry
  in the table takes effect without recompiling.
- **Depends on:** F-20, F-21, F-11, F-50.
- **Milestone:** M4.

#### F-52 — Idle and ambient behaviour

- **Goal:** the Furby feels alive when nobody is talking to it.
- **Description:** when idle, the Furby blinks and shifts occasionally at randomised intervals.
  In darkness it goes to sleep — moving into the sleeping pose, closing its eyes and going quiet
  — and wakes on light or on touch, with an appropriate animation. Idle behaviour respects quiet
  hours and yields immediately to any real interaction.
- **Acceptance:** left alone in a lit room, the Furby blinks at irregular, natural-looking
  intervals; covering it makes it fall asleep within a defined time; uncovering or touching it
  wakes it; during quiet hours it stays still.
- **Depends on:** F-11, F-21, F-45.
- **Milestone:** M4.

### Smart Home Integration

#### F-60 — Phrase catalogue

- **Goal:** prepared events produce funny, varied, mode-appropriate phrases.
- **Description:** the catalogue of ~52 prepared phrases ([Appendix C](#appendix-c--phrase-catalogue))
  lives in **Home Assistant**, not on the device. It is structured by trigger, with two
  modifiers already present in the source material:
  - **`-Serious` variants** — a second, escalated line, used when the event repeats or persists.
  - **`Variant-*` entries** — interchangeable alternatives, picked at random so the Furby does
    not repeat itself.

  Selection logic: pick the entry for the trigger, apply the escalation rule if the event is a
  repeat, choose randomly among variants, avoid repeating the most recent choice, then send it
  to the device with the current mode.
- **Acceptance:** each prepared event produces its phrase; a repeated event within the
  escalation window produces the `-Serious` line; triggering an event with variants five times
  does not repeat the same line twice in a row; adding a phrase requires no firmware change.
- **Depends on:** F-61, F-43.
- **Open question:** the exact semantics of `-Serious` — see [§13](#13-open-questions--decision-log).
- **Milestone:** M5.

#### F-61 — Speak interface

- **Goal:** Home Assistant can make the Furby say anything, with expression.
- **Description:** a single call the device exposes: **speak(text, mode, animation, priority)**.
  The device requests the spoken rendering in the given mode, plays it, animates accordingly,
  and enforces quiet hours and volume. Priority decides what happens when something is already
  playing: queue, interrupt, or drop. An alarm-class phrase (fire, water, intruder) must
  interrupt and must ignore the quiet-hours suppression.
- **Acceptance:** calling it from Home Assistant makes the Furby speak with the right voice and
  movement; a normal phrase during an ongoing utterance queues rather than overlapping; an
  alarm-class phrase interrupts immediately and plays even at night.
- **Depends on:** F-40, F-45, F-50, F-11.
- **Milestone:** M5.

#### F-62 — Events to Home Assistant

- **Goal:** everything the Furby notices is available for automation.
- **Description:** the device emits events for sensor triggers (tummy, back, tongue, tilt, light
  change), IR messages received, mode changes, assistant lifecycle, and faults. Event names are
  stable and documented, so automations do not break.
- **Acceptance:** every event appears in Home Assistant with a stable name and useful payload; an
  automation can be built on each of them; the naming is documented in this FSD.
- **Depends on:** F-20, F-21, F-30, F-50.
- **Milestone:** M5.

#### F-63 — Diagnostics

- **Goal:** the Furby's health is visible without opening it.
- **Description:** exposed diagnostics: uptime, WiFi signal, free memory, current cam position
  and homing status, motor fault counters and duty usage, last assistant error, microphone
  level, current mode and volume.
- **Acceptance:** a Home Assistant dashboard shows all of them; a deliberately induced motor
  stall increments the fault counter visibly; the homing status correctly reads "not homed"
  after a fault.
- **Depends on:** F-10, F-42.
- **Milestone:** M5.

### Operations

#### F-70 — Update and recovery

- **Goal:** the Furby can always be recovered without disassembly.
- **Description:** over-the-air updates, plus a safe mode that comes up with networking and
  update capability only, entered automatically after repeated boot failures. Physical
  disassembly must never be the only recovery path.
- **Acceptance:** an OTA update succeeds from the normal state; deliberately flashing a broken
  build results in safe mode, from which a good build can be flashed over the air.
- **Depends on:** F-01.
- **Milestone:** M6.

#### F-71 — Quiet-hours enforcement

- **Goal:** the quiet-hours policy is enforced consistently everywhere.
- **Description:** a single policy point that every output path consults — speech, motor,
  eye brightness, idle behaviour. Not a rule re-implemented per feature. Includes the alarm-class
  override from [F-61](#f-61--speak-interface).
- **Acceptance:** each output type is verifiably suppressed or attenuated at the boundary time;
  an alarm-class phrase overrides it; there is exactly one place in the code where the policy
  is decided.
- **Depends on:** F-45.
- **Milestone:** M6.

#### F-72 — Motor safety limits

- **Goal:** the 28-year-old gearbox survives.
- **Description:** a maximum continuous run time, a duty-cycle limit over a rolling window, a
  cool-down after heavy use, and a hard stop plus fault on stall. Talk animation
  ([F-12](#f-12--talk-animation)) and idle behaviour are the main consumers and must both be
  throttled by this budget.
- **Acceptance:** a five-minute continuous talk animation stays within the duty budget by
  thinning out movement rather than running the motor constantly; a stall stops the motor
  within the timeout; the fault is visible in diagnostics and clears on a successful re-home.
- **Depends on:** F-10.
- **Milestone:** M6.

---

## 10. Home Assistant Interface

The contract below must be satisfied by whichever framework path is chosen.

### 10.1 Exposed controls

| Control | Type | Purpose |
| --- | --- | --- |
| Mode | select (`normal` / `cute` / `evil`) | Read and set the personality |
| Volume | number (%) | Current speaking volume |
| Eyes | light | Colour and brightness |
| Quiet hours enabled | switch | Master switch for the schedule |
| Play animation | action (name) | Trigger a named animation |
| Speak | action (text, mode, animation, priority) | The [F-61](#f-61--speak-interface) entry point |
| Send IR message | action (message name) | [F-31](#f-31--infrared-transmit) |
| Re-home | action | Force a homing cycle |

### 10.2 Emitted events

Names are stable. Payloads carry at least the triggering source and a timestamp.

| Event | Fired when |
| --- | --- |
| `furby.touched` | A switch produced a derived event (with `which` and `kind`) |
| `furby.light_changed` | Significant brightness change (with `direction`) |
| `furby.ir_received` | A Furby IR message was recognised (with `message`) |
| `furby.mode_changed` | Mode changed (with `from`, `to`) |
| `furby.assist` | Assistant lifecycle transition (with `state`) |
| `furby.fault` | A motor or subsystem fault (with `kind`) |

### 10.3 Naming conventions

- Device/host name: `d03n3rFurby` (carried over from the legacy project).
- Entities are prefixed with the device name by the integration; feature IDs are **not** part of
  entity names.
- Event names are lower snake case under the `furby.` namespace.

---

## 11. Non-functional Requirements

| ID | Requirement |
| --- | --- |
| NFR-1 | **Mechanical safety.** The motor must never run continuously beyond its configured limit, and a stall must stop drive within a bounded time. |
| NFR-2 | **Graceful degradation.** Without WiFi or Home Assistant, the Furby still moves, blinks and reacts to touch. It only loses speech. |
| NFR-3 | **Recoverability.** Firmware recovery never requires opening the Furby. |
| NFR-4 | **Quiet by default.** No unprompted noise or movement outside the configured windows, except alarm-class events. |
| NFR-5 | **Stability.** The device runs for at least a week without a reboot, memory leak or degraded response. |
| NFR-6 | **Latency.** Push-to-talk to "listening" feedback is perceptually immediate; the end-to-end voice interaction is not noticeably slower than a comparable Home Assistant voice satellite. |
| NFR-7 | **Editability.** Phrases, the pose table and the reaction table can all be changed without recompiling firmware. |
| NFR-8 | **No secrets in the repository.** Credentials and tokens come from an ignored local file. |

---

## 12. Repository Layout

To be finalised with [ARCH-D1](#5-architecture-decision-arch-d1--framework); the documentation
part applies either way.

```
esp32-furby/
├── README.md
├── docs/
│   ├── FSD.md                  ← this document
│   ├── hardware.md             ← pin map, wiring, measurements (from M0)
│   └── decisions/              ← one file per resolved decision, if they grow
├── firmware/                   ← ESPHome YAML + components, or PlatformIO project
└── homeassistant/
    ├── packages/               ← phrase catalogue, event automations
    └── blueprints/
```

---

## 13. Open Questions & Decision Log

### 13.1 Open decisions

| ID | Decision | Blocks | Status |
| --- | --- | --- | --- |
| ARCH-D1 | ESPHome vs. custom firmware vs. hybrid | Everything | Open — gated on HW-D3 |
| HW-D1 | ESP32 classic vs. ESP32-S3 | F-01, F-40, F-41 | Open |
| HW-D2 | Original electret vs. MEMS microphone | F-41 | Open — measurement pending |
| HW-D3 | Speaker and amplifier path | ARCH-D1, F-40 | Open — measurement pending |
| HW-D4 | Motor driver | F-10 | Open — leaning TB6612FNG |
| HW-D5 | Power source and rail separation | F-10 | Open — measurement pending |
| HW-D6 | GPIO budget / port expander | All | Open — depends on enclosure |
| HW-D7 | Wake word strategy | F-42 | Open — push-to-talk first regardless |
| HW-D8 | Eye LED type | F-22 | Open — confirm what is already wired |

### 13.2 Open questions

1. **`-Serious` semantics.** The catalogue pairs several phrases with a `-Serious` variant. The
   assumed reading is *"the event repeated or persisted, so escalate"*. Some entries
   (`AirQuality-Serious`, `ClimateWarning-Serious`) instead read like a *reply to an announcement
   another system just made*. Which is intended — or is it both, depending on the entry?
2. **Cam position table.** The named poses in [F-11](#f-11--named-poses-and-animation-sequencer)
   must be calibrated against the actual gearbox. Is a reference table from the original
   available, or do we measure all of them empirically during M2?
3. **Eye LED wiring.** What is already installed — addressable or discrete? ([HW-D8](#hw-d8--eye-leds))
4. **Battery.** Is battery operation wanted? The catalogue contains a low-battery phrase
   (`003-Low-Battery`), which suggests yes, but it also constrains the enclosure.
5. **Wake word phrase.** If on-device wake word is pursued, which phrase? "Hey Förby" would need
   a custom-trained German model.
6. **Second Furby.** Is one available for testing [F-30](#f-30--infrared-receive) /
   [F-31](#f-31--infrared-transmit), or do we test against recorded codes only?
7. **Meaning of IR messages #2 and #8.** Their frames are known and reproducible
   ([A3](#a3-the-infrared-protocol-decoded)), but not what they say. Two ways to find out:
   transmit them at a real Furby and watch, or mine the original source listing
   ([A8](#a8-sources)) once it is reachable.

### 13.3 Resolved decisions

| Decision | Resolution | Date |
| --- | --- | --- |
| Speech source | All speech is live Home Assistant TTS; no pre-rendered on-device samples | 2026-09-04 |
| Behaviour depth | Reactions, animations and modes; **no** needs/mood simulation | 2026-09-04 |
| Motion sensing | Original gearbox retained, using **both** the optical encoder and the sync switch | 2026-09-04 |
| Framework stance | ESPHome preferred but not mandatory; reusing original parts ranks higher | 2026-09-04 |
| Documentation language | English, except the German phrase content | 2026-09-04 |

---

## Appendix A — Salvaged from the legacy sketch

Preserved from `SmartFurby.ino` and `config.h` because it encodes real hardware knowledge.

### A1. Legacy pin map

| Signal | GPIO | Notes |
| --- | --- | --- |
| Status LED | 2 | On-board LED |
| Light sensor (LDR) | 34 | Input-only pin, ADC |
| Cam position sensor | 18 | `INPUT_PULLUP` — the optical encoder |
| IR receiver (head) | 19 | `INPUT_PULLUP` |
| IR transmit LED (head) | 27 | |
| Motor PWM | 13 | LEDC |
| Motor forward | 12 | |
| Motor backward | 14 | |
| MP3 module RX / TX | 16 / 17 | Obsolete — the DFPlayer is dropped |

Note: the legacy map has **no sync switch pin**. Since the sync switch is now confirmed as
wired, the new pin map must add it — see `docs/hardware.md` (to be created in M0).

### A2. Motor drive parameters

```
LEDC channel      1
PWM frequency     12000 Hz
PWM resolution    8 bit
Running duty      150 / 255   (~59 %)
```

Direction change procedure, worth keeping — it prevents shoot-through and gearbox shock:

1. Stop: both direction pins LOW, duty 0.
2. Wait 10 ms.
3. Set the new direction pin HIGH (the other stays LOW).
4. Apply the running duty.

### A3. The infrared protocol, decoded

The legacy `config.h` held 14 raw timing arrays with no explanation of what they encoded. They
have since been decoded completely, and the protocol is now specified rather than copied.

**How it was derived.** `mrtee/furby-ir` (see [Sources](#a8-sources)) contains a PIC assembly
transmitter for this protocol. Its bit routine emits a pulse, sleeps, conditionally emits a
second pulse, then sleeps again — which makes a `1` two pulses separated by one sleep, and a `0`
one pulse followed by two sleeps. The frame is a start bit plus 8 bits shifted out right-first
(`rrf`), i.e. LSB first, and the byte is built by taking the message number, swapping the
nibbles, inverting the upper four bits and OR-ing them back in — producing `~N` in the high
nibble as a checksum. Each frame is sent 6 times.

**How it was verified.** Applying that rule to all 14 captured arrays yields a valid checksum
(`high == ~low`) in **14 of 14** cases, and re-encoding each message number reproduces the
captured array **byte for byte**. Independently, message #5 decodes to `PARTY2`, matching the
documented fact that message #5 is "Party!". The chance of 14 independent arrays satisfying a
4-bit checksum by luck is 16⁻¹⁴.

**Generating a frame** for message number `N` (0–15):

```
byte  = ((~N & 0x0F) << 4) | (N & 0x0F)
frame = [1] + [ (byte >> i) & 1  for i in 0..7 ]     # start bit, then LSB first
raw   = for each bit:  1 -> mark, short gap, mark, short gap
                       0 -> mark, long gap
```

Drop the trailing gap: a receiver ends the capture on timeout, so a captured frame is 27
entries — 14 marks and 13 gaps. In the captured data a mark and a short gap are ~275 µs and a
long gap ~850 µs; treat these as *observed* values and re-measure rather than hard-coding them,
since only the short/long ratio is structural.

**The message table.** Numbers and checksums are derived; the names come from the legacy
capture; the meaning column is what the name implies, not independently confirmed.

| # | Byte | Legacy name | Presumed meaning |
| ---: | --- | --- | --- |
| 0 | `0xF0` | `HELLO3` | Greeting |
| 1 | `0xE1` | `HELLO1` | Greeting |
| 2 | `0xD2` | *(reconstructed)* | **Unknown** |
| 3 | `0xC3` | `HELLO2` | Greeting |
| 4 | `0xB4` | `PARTY1` | Party |
| 5 | `0xA5` | `PARTY2` | Party — confirmed as "Party!" by an external source |
| 6 | `0x96` | `DANCE1` | Dance |
| 7 | `0x87` | `DANCE2` | Dance |
| 8 | `0x78` | *(reconstructed)* | **Unknown** |
| 9 | `0x69` | `MESING1` | "Me sing" |
| 10 | `0x5A` | `JOKE1` | Joke |
| 11 | `0x4B` | `YOUSING1` | "You sing" |
| 12 | `0x3C` | `HIDE2` | Hide and seek |
| 13 | `0x2D` | `HIDE1` | Hide and seek |
| 14 | `0x1E` | `YAWN1` | Yawn |
| 15 | `0x0F` | `SLEEP1` | Sleep |

**The two previously missing codes.** Generated from the rule above and therefore as reliable as
the 14 that round-trip exactly — but **their meaning is unknown**, and they have never been sent
at a real Furby. Verify before relying on them.

```c
// message #2 — byte 0xD2, frame 1 01001011
const uint16_t irUNKNOWN2[] PROGMEM = { 275U, 275U, 275U, 275U, 275U, 850U, 275U, 275U, 275U, 275U, 275U, 850U, 275U, 850U, 275U, 275U, 275U, 275U, 275U, 850U, 275U, 275U, 275U, 275U, 275U, 275U, 275U };

// message #8 — byte 0x78, frame 1 00011110
const uint16_t irUNKNOWN8[] PROGMEM = { 275U, 275U, 275U, 275U, 275U, 850U, 275U, 850U, 275U, 850U, 275U, 275U, 275U, 275U, 275U, 275U, 275U, 275U, 275U, 275U, 275U, 275U, 275U, 275U, 275U, 275U, 275U };
```

Note that #2 sits between two greetings (#1 and #3) and #8 between the dance and sing groups, so
both plausibly belong to those clusters — but that is inference from neighbours, not evidence.

The original 14 arrays remain in the legacy `config.h` in the project history; they are no
longer worth carrying forward, since any of the 16 frames is now generated from its number.

### A4. Infrared matching

The legacy matcher slid the candidate pattern along the captured buffer and accepted a timing
within a tolerance window:

```
tolerance = expected * 30 %      (but never less than 100 µs)
match     = expected - tolerance < actual < expected + tolerance
```

On a mismatch the match index reset to zero and the scan continued — a sliding-window matcher
that tolerates a noisy leading edge. It is robust and cheap, but obsolete: now that the frame
format is known ([A3](#a3-the-infrared-protocol-decoded)), the receiver should classify each gap
as short or long, assemble the 9 bits, and validate the nibble checksum. That is cheaper, works
for all 16 messages without a table, and rejects non-Furby signals outright.

### A5. Quiet hours and volume schedule

```
motor allowed     08:00 – 20:00
speech allowed    08:00 – 22:00
volume            75 %   09:00 – 19:00
                  50 %   08:00 – 09:00 and 19:00 – 20:00
                  25 %   otherwise
```

### A6. Light sensor threshold

The LDR was sampled continuously and evaluated once per second; a change of more than **10 % of
the current reading** within that second counted as a significant brightening or darkening.
(The legacy implementation had the two branches inverted relative to their comments — worth
noting so the bug is not reproduced.)

### A7. Deliberately dropped

| Legacy element | Why dropped |
| --- | --- |
| DFPlayer Mini + SD card samples | Replaced by live Home Assistant TTS |
| MQTT client and topic scheme | Replaced by the native Home Assistant path |
| Hand-rolled WiFi scan/connect logic | Provided by the framework |
| Daily scheduled restart | A workaround for leaks; NFR-5 requires fixing the cause instead |
| `setCpuFrequencyMhz(80)` | Audio work needs the headroom |

### A8. Sources

External material this document draws on, so the reasoning can be re-checked later.

| Source | Used for |
| --- | --- |
| [Furby 1998 source code](https://archive.org/details/furby-source) — the original SPC81A assembly listing by David Hampton and Wayne Schulz, scanned by Sean Riddle (also as [PDF](https://www.seanriddle.com/furbysource.pdf) and as [plain text](https://archive.org/stream/furby-source/furbysource_djvu.txt)) | The authoritative reference for original firmware behaviour. **Not yet consulted** — archive.org is unreachable from the development session's network, which is restricted to GitHub and a few other hosts. Worth mining later for cam positions, sensor timings and the meaning of IR messages #2 and #8. |
| [`mrtee/furby-ir`](https://github.com/mrtee/furby-ir) — PIC assembly transmitter for the Furby IR protocol | The bit encoding, frame layout, nibble checksum and repeat behaviour that made [A3](#a3-the-infrared-protocol-decoded) possible. |
| [`d03n3rfr1tz3/TTGO.T-Watch.2020`](https://github.com/d03n3rfr1tz3/TTGO.T-Watch.2020) | The voice assistant building blocks in [Appendix B](#appendix-b--reusable-from-the-t-watch-project). |
| [ESPHome documentation](https://esphome.io) (`esphome/esphome-docs`) | The framework constraints in [ARCH-D1](#5-architecture-decision-arch-d1--framework) — notably that the internal microphone ADC and internal DAC are no longer supported. |

---

## Appendix B — Reusable from the T-Watch project

Source: `d03n3rfr1tz3/TTGO.T-Watch.2020`. Relevant if [ARCH-D1](#5-architecture-decision-arch-d1--framework)
lands on the custom-firmware path — most of the hard work is already done there.

### B1. Home Assistant assist client

`src/app/assist/assist_ws.{h,cpp}` — a WebSocket client against Home Assistant's
`/api/websocket`:

- Authentication with a long-lived access token, including requesting a token during pairing.
- Fetching and selecting the Assist pipeline (the preferred one, or a named one).
- Starting a run and driving a state machine: `STARTING` → `LISTENING` → `THINKING` → `DONE`
  / `FAILED`, with per-run message IDs so late events from an abandoned run are discarded.
- Receiving the transcript, the answer text, and the TTS audio URL.
- Timeouts on both sides: a run timeout slightly above the one Home Assistant applies to itself.

`src/app/assist/assist_tts.{h,cpp}` — fetches the TTS audio over HTTP into RAM and plays it,
with a size cap beyond which the answer stays text-only.

### B2. Audio framing

Audio is sent as binary WebSocket frames: a **handler byte** followed by **signed 16-bit
little-endian mono PCM**, 1024 bytes per frame. A lone handler byte signals end-of-audio. This
is the wire format the Furby needs too.

### B3. Audio DSP parameters

`src/app/assist/assist_stream.{h,cpp}` — good starting values for [F-41](#f-41--microphone-chain):

| Parameter | Value | Purpose |
| --- | --- | --- |
| Ring buffer | 64 KB (~2 s) | Headroom against a stalling WiFi link |
| Block size | 512 samples | Per microphone read |
| High-pass | 250 Hz Butterworth at 16 kHz | Removes rumble and handling noise |
| Limiter ceiling | −12 dBFS | Prevents clipping on close speech |
| Limiter look-ahead | 160 samples (10 ms) | Gain is in place before the peak arrives |
| Limiter attack / release | ~4 ms / ~90 ms | Fast enough to catch plosives without pumping |
| Gain options | off, +18, +30, +36, +42 dB | Default +36 dB — speech at a few cm lands near −20 dBFS |
| Hard cap | 15 s | The reader stops itself |

### B4. Microphone and speaker drivers

- `src/hardware/micctl.{h,cpp}` — PDM MEMS microphone (SPM1423HM4H-B) via ESP32 I²S **PDM RX**
  (available on I2S0 only), 16 kHz / 16-bit, two pins (clock + data), with a deferred-stop
  mechanism so brief UI transitions do not tear the microphone down.
- `src/hardware/sound.{h,cpp}` — ESP8266Audio (earlephilhower): `AudioOutputI2S` plus generators
  for MP3, WAV and RTTTL, and sources for SPIFFS, PROGMEM and RAM. The **RAM source** is exactly
  the path a fetched TTS response needs; RTTTL is a cheap way to give the Furby jingles without
  any audio files.

---

## Appendix C — Phrase catalogue

The 52 prepared phrases, in German. Lives in Home Assistant per
[F-60](#f-60--phrase-catalogue).

**Conventions:** `-Serious` marks an escalated/follow-up line for the same trigger (semantics to
be confirmed — see [§13.2](#132-open-questions)). `Variant-*` entries are interchangeable
alternatives chosen at random.

### C1. System

| Key | Phrase |
| --- | --- |
| `001-Cute-Boot` | Förby einsatzbereit! |
| `001-Evil-Boot` | Förby einsatzbereit! |
| `002-Cute-Mode` | Zerstörungsmodus, deaktiviert! |
| `002-Evil-Mode` | Zerstörungsmodus, aktiviert! |
| `003-Low-Battery` | Förby nach Hause transportieren! |

### C2. House and doorbells

| Key | Phrase |
| --- | --- |
| `DoorBell` | Mach doch mal einer die Tür auf! |
| `DoorBell-Serious` | Alter, ist da jemand auf der Klingel eingeschlafen? |
| `Door-Open-from-Outside` | Willkommen zu Hause, Bewohner! |
| `MailBell` | Wer hat denn nun schon wieder was in den Briefkasten geworfen? |
| `MailBell-Serious` | Alter, wie lange kann man denn brauchen um etwas in den Briefkasten zu legen? |
| `PhoneBell` | Seid ihr taub? Das Telefon hat gebimmelt! |
| `PhoneBell-Serious` | Seid ihr taub? Das Telefon hat ganz schön lange gebimmelt! |
| `PrinterBell` | Hui, da liegt was neues im 3D Drucker! |
| `WashBell` | Die Wäsche ist fertig gewaschen. Kümmer sich doch mal einer darum! |
| `DishBell` | Hey, der Spüli ist fertig! |
| `BirdBell` | Heeh? Wenn dann hast du hier einen Vogel! |

### C3. Climate and air

| Key | Phrase |
| --- | --- |
| `AirQuality` | Bah, hier ist dicke Luft! |
| `AirQuality-Serious` | Ihr habt die Frau gehört, jetzt macht doch endlich mal! |
| `AirQuality-OpenWindow` | Ja verdammt, machen wir doch schon! |
| `AirHumidity` | Heul doch! |
| `AirHumidity-Serious` | Ich schick dich gleich in die Wüste? |
| `ClimateWarning` | Panik, Panik! |
| `ClimateWarning-Serious` | Klingt schlau was die Frau da sagt. |
| `Balcony-Open-Cold` | Pass auf, es ist kalt draußen! |
| `Balcony-Open-Hot` | OMG, was tust du? Es ist viel zu warm draußen! |

### C4. Weather

| Key | Phrase |
| --- | --- |
| `WeatherWarning-Rain` | Ach, wir sind doch nicht aus Zucker! |
| `WeatherWarning-Rain-Serious` | Bestimmt nur bisschen Nieselregen! |
| `WeatherWarning-Sun` | Genau, sonst verbrennst du dich! |
| `WeatherProbe` | Hui, da bin ich schon ganz aufgeregt! |
| `WeatherProbe-Serious` | Schnell, schnapp sie dir! |

### C5. Alarms

These are the **alarm class** referenced in [F-61](#f-61--speak-interface): they interrupt and
override quiet hours.

| Key | Phrase |
| --- | --- |
| `FireAlarm` | FEUER, FEUER, FEUER! Alle raus hier! |
| `WaterAlarm` | Ohje, da ist Wasser, wo es nicht hingehört! |
| `IntruderAlarm` | Oh, hallo lieber Einbrecher. Bitte freundlich lächeln für die Kamera! |

### C6. Media and gaming

| Key | Phrase |
| --- | --- |
| `Kodi-Movie-Start` | Ist der Film auf deutsch? Weil, my english is not the yellow from the egg. |
| `Kodi-Music-Start` | Yeah, machen wir jetzt eine PAAHRTYYYY?? |
| `Kodi-Show-Start` | Oh supi, die Folge hab ich lange nicht mehr gesehen! |
| `Twitch-Start` | Ist das dieses Schtreaming von dem immer alle reden? |
| `Twitch-Stop` | Das war ein schöner Stream heute. |
| `RetroPie-Start` | Ich möchte ein Spiel spielen! |
| `RetroPie-Stop` | Och, noch fünf Minuten bitte! |
| `RetroPie-Variant-GB` | Hach, der GameBoy, mein alter Kumpel |
| `RetroPie-Variant-N64` | Ein bisschen Nintento 64 geht immer! |
| `RetroPie-Variant-PSX` | Jaa, lass uns PlayStation spielen! |
| `RaidLog` | Und, bist du Top DPS? |
| `RocketLog` | Und, hast du gewonnen? |

### C7. Presence (PaxCount)

| Key | Phrase |
| --- | --- |
| `PaxCount` | Sei doch ruhig da drüben! |
| `PaxCount-Serious` | Ja die sind alle nur hier um mich zu bestaunen! |
| `PaxCount-Variant-Bananen` | Was, Bananen, wo? BANANANA-BANANANANEN, will ich auch! |
| `PaxCount-Variant-Freibier` | Ich würd auch ein Bier nehmen, bitte! |
| `PaxCount-Variant-Party` | Siehst du hier jemanden feiern? |

### C8. Other devices

| Key | Phrase |
| --- | --- |
| `UselessBox-Idle-Pre` | Hey Tucan, alles fit in deiner Kiste? |
| `UselessBox-Idle-Post` | Ganz deiner Meinung, Tucan! |

**Total: 52 phrases.**

---

## Glossary

| Term | Meaning |
| --- | --- |
| **Cam angle** | Position of the camshaft in encoder steps, 0 … ~415. The single coordinate of all Furby movement. |
| **Homing** | Driving the camshaft to the sync switch to establish an absolute zero. |
| **Sync switch** | The original absolute position reference in the gearbox. |
| **Optical encoder** | LED + phototransistor reading slots in a base gear; produces the step count. |
| **Pose** | A named cam angle, e.g. `eyes_closed`. |
| **Animation** | A named sequence of poses with timing. |
| **Mode** | The Furby's personality: `normal`, `cute` or `evil`. |
| **Alarm class** | Phrases that interrupt playback and override quiet hours. |
| **Assist pipeline** | Home Assistant's speech-to-text → intent → text-to-speech chain. |
| **Quiet hours** | Time windows restricting unprompted speech, movement and light. |
| **`-Serious` variant** | An escalated/follow-up phrase for a repeated or persistent event. |
