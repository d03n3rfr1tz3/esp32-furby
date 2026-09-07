# Functional Specification Document — ESP32 Furby

**Project:** `esp32-furby` — a 1998 Furby refurbished with an ESP32 brain
**Status:** Draft 2 — framework and transport decided, hardware decisions partly open
**Last updated:** 2026-09-07

---

## 1. Purpose & Scope

### 1.1 What this document is

A living specification of *every* planned feature of the project. It exists so that each
working session can pick **exactly one feature** (or less), plan it in detail, implement it and
test it — without re-deriving the overall design each time.

Features are written **functionally**: what the Furby must do, and how we know it works. The
framework question that used to keep them deliberately unbound is now settled — see
[§5](#5-architecture-decisions) — so a feature carries a single *Implementation note* where the
path is not obvious. The functional wording stays, because it is what the acceptance criteria
are tested against.

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
engine with DSP, a PDM microphone driver and an audio output layer. This proved the
custom-firmware path viable, which is a large part of why
[ARCH-D1](#53-arch-d1-resolution) went the way it did.

What it still supplies directly is the **microphone, DSP and audio layer**
([B3](#b3-audio-dsp-parameters), [B4](#b4-microphone-and-speaker-drivers)). Its **assist
transport** ([B1](#b1-home-assistant-assist-client), [B2](#b2-audio-framing)) is superseded by
[ARCH-D2](#54-arch-d2--home-assistant-voice-transport) — see
[Appendix B](#appendix-b--reusable-from-the-t-watch-project) for what carries over and what does
not.

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
2. **Framework comfort never outranks an original part.** This principle used to read "ESPHome is
   preferred, not mandatory"; [ARCH-D1](#53-arch-d1-resolution) applied it and landed on custom
   firmware. The principle itself stands and still decides the cases that come after it.
3. **Home Assistant owns language.** All text and all voices live in Home Assistant. The device
   holds *behaviour*, not a phrase database. This keeps phrases editable without a reflash.
4. **One feature per session.** Every feature must be independently testable on real hardware.
5. **The Furby must not be annoying.** Quiet hours, motor duty limits and volume scheduling are
   first-class requirements, not polish.
6. **Fail soft.** Losing WiFi or Home Assistant must degrade the Furby, not brick it. It should
   still respond physically to being touched.

---

## 5. Architecture Decisions

| ID | Decision | Status |
| --- | --- | --- |
| [ARCH-D1](#51-the-core-tension-arch-d1) | Firmware framework | **Resolved 2026-09-07 — custom firmware (path B)** |
| [ARCH-D2](#54-arch-d2--home-assistant-voice-transport) | Home Assistant voice transport | **Resolved 2026-09-07 — Wyoming satellite** |
| [ARCH-D3](#55-arch-d3--framework-flavour-and-toolchain) | Framework flavour and toolchain | **Resolved 2026-09-07 — PlatformIO + Arduino via pioarduino** |
| [ARCH-D4](#56-arch-d4--control-and-event-transport) | Control and event transport | Open — MQTT with HA discovery recommended |

### 5.1 The core tension (ARCH-D1)

*This comparison and [§5.2](#52-decision-aids) are kept as the record of why ARCH-D1 went the way
it did. The resolution is in [§5.3](#53-arch-d1-resolution).*

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

### 5.3 ARCH-D1 resolution

**Decision: path B — custom firmware.** ESPHome and the hybrid path are dropped.

Two constraints that were not on the table when [§5.1](#51-the-core-tension-arch-d1) was written
settled it:

1. **Space.** A wireless charging circuit is being added ([HW-D5](#hw-d5--power)). It consumes
   the volume that add-on modules would have needed, so the build has to stay minimal. Path A
   *mandates* a digital microphone and an I²S amplifier; path B does not mandate anything.
2. **Audio stack.** ESPHome's audio limitations — no internal microphone ADC, no internal DAC —
   are the one area where this project is unusually demanding, which makes them close to
   disqualifying on their own.

#### Why this does not pre-empt HW-D3

Earlier drafts gated ARCH-D1 on [HW-D3](#hw-d3--speaker-and-amplifier), reasoning that a
disappointing original speaker would make ESPHome attractive. **That coupling pointed the wrong
way.** Path B works with *either* audio path — internal DAC or I²S amplifier — while path A only
works with the digital one. Choosing B therefore forecloses nothing that HW-D3 still has to
decide; it is the option that keeps the most open. The gate is removed rather than overridden,
and HW-D2/HW-D3 stay open on their own merits.

#### What the decision costs

- **The audio stack is ours.** Capture, DSP, playback and their bugs are maintained in this
  repository forever. [Appendix B](#appendix-b--reusable-from-the-t-watch-project) is what makes
  that affordable.
- **No `micro_wake_word`.** On-device wake word is off the table (it needs ESPHome in practice,
  and an ESP32-S3). It is replaced by server-side wake word — see
  [HW-D7](#hw-d7--wake-word-strategy) — which turns out to cost nothing.
- **No free Home Assistant entities.** The native API is gone with ESPHome; see
  [ARCH-D4](#56-arch-d4--control-and-event-transport).

### 5.4 ARCH-D2 — Home Assistant voice transport

**Decision: speak the Wyoming protocol and appear as a real Assist satellite.**

| Option | Assessment |
| --- | --- |
| **Raw `assist_pipeline/run` WebSocket** (what the T-Watch does) | Viable and not deprecated — the command is unchanged in `home-assistant/core`, carries `wake_word`/`stt`/`intent`/`tts` start stages and the `stt_binary_handler_id` binary framing, and the Home Assistant frontend's own Assist dialog runs on it. Its limitation is structural: a WebSocket client is a *client*, never a satellite, so satellite-only capabilities never reach it. |
| **Wyoming satellite** *(chosen)* | Home Assistant's `wyoming` integration builds a full `assist_satellite` entity from a Wyoming device, with `announce` implemented and timer events included. It is the versioned protocol explicitly intended for third-party satellites. |

**Rationale.** The risk with the raw WebSocket path is not removal, it is a **ceiling**: new
satellite features (`announce`, `start_conversation`, timers, the Voice-assistants satellite UI)
attach to the `assist_satellite` entity platform. Wyoming sits above that ceiling, and three
consequences make it the better fit here rather than merely the safer one:

- Home Assistant **connects to the satellite** (TCP), so the device holds **no long-lived access
  token** — [NFR-8](#11-non-functional-requirements) gets easier, not harder.
- Text-to-speech arrives as **raw PCM chunks**, so no MP3 or WAV decoder is needed on the device
  for assistant answers (see [F-40](#f-40--audio-output-chain)).
- [F-61 `speak()`](#f-61--speak-interface) maps directly onto `assist_satellite.announce`.

**Cost.** [Appendix B1](#b1-home-assistant-assist-client) and
[B2](#b2-audio-framing) — the T-Watch assist client and its wire format — are largely superseded.
[B3](#b3-audio-dsp-parameters) and [B4](#b4-microphone-and-speaker-drivers) are unaffected, and
they are the larger part of the work.

**Limit of scope — read this before designing [§10](#10-home-assistant-interface).** Wyoming
solves *voice*, not *integration*. A Wyoming satellite entity provides `assist_satellite` and a
wake-word select, and nothing else: none of the controls in
[§10.1](#101-exposed-controls) and none of the events in [§10.2](#102-emitted-events) come from
it. That is what [ARCH-D4](#56-arch-d4--control-and-event-transport) is for.

### 5.5 ARCH-D3 — Framework flavour and toolchain

**Decision: PlatformIO with the Arduino-ESP32 3.x core, via the `pioarduino` platform fork.**

| Option | Assessment |
| --- | --- |
| **Arduino via `pioarduino`** *(chosen)* | Maximum reuse of the T-Watch modules; ESP8266Audio works directly. |
| ESP-IDF native | Better control over RAM, DMA and I²S, but ESP8266Audio is lost and more of the port becomes hand work. |
| Arduino as an ESP-IDF component | Both API sets available, but the fiddliest setup under PlatformIO. |

**Toolchain note, and it is load-bearing for [F-02](#f-02--repository-layout-build-and-validation):**
the official `platformio/platform-espressif32` still ships the Arduino 2.x core. Arduino-ESP32
3.x on ESP-IDF 5.5 comes from the community fork `pioarduino/platform-espressif32`, which
`platformio.ini` must therefore name explicitly. The dependency on a community fork is the
accepted price.

### 5.6 ARCH-D4 — Control and event transport

**Status: open.** [ARCH-D2](#54-arch-d2--home-assistant-voice-transport) covers voice only, so a
second transport has to carry the control surface and the events of
[§10](#10-home-assistant-interface).

| Option | Assessment |
| --- | --- |
| **MQTT with Home Assistant discovery** *(recommended)* | Entities appear without writing a Home Assistant integration; events become topics; the legacy sketch already spoke MQTT, so the operational pattern is familiar. Adds a broker as a dependency. |
| A custom Home Assistant integration over a private WebSocket/HTTP API | Full control over the entity model; a HACS component to write and maintain. |
| REST endpoints on the device plus `rest_command` / template entities | No broker, but the entity model has to be assembled by hand in YAML, and events need polling or webhooks. |

Resolve before [M4](#8-milestones); [F-50](#f-50--mode-manager) is the first feature that needs
it.

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
| Voice / mode rendering | Requests a mode; plays the returned PCM | ✅ picks the TTS voice per mode and renders it |
| Wake word | Streams while docked; push-to-talk otherwise ([HW-D7](#hw-d7--wake-word-strategy)) | ✅ runs the detection (openWakeWord) |
| Speech-to-text, intents | Streams audio | ✅ runs the pipeline |
| Motion, poses, animation | ✅ owns entirely | May request a named animation |
| Mode state (normal/cute/evil) | ✅ owns, persists across reboot | Can set and read it |
| Sensor reactions | ✅ owns the mapping | Can override / extend via events |
| Quiet hours, volume schedule | ✅ enforces | May configure the schedule |
| Event → phrase automations | Emits events | ✅ decides what to say |

**Rationale:** the device must stay useful and physically alive when Home Assistant is
unreachable — it can still move, blink and react to touch. It just goes quiet.

### 6.3 Module boundaries

All modules are plain firmware modules under [ARCH-D1](#53-arch-d1-resolution).

| Module | Responsibility | Implementation |
| --- | --- | --- |
| `furby_motion` | Motor drive, encoder counting, homing, cam angle control, safety | Plain C++; interrupt-driven encoder counter |
| `furby_pose` | Named poses, animation sequencer, talk animation | Plain C++ over `furby_motion` |
| `furby_sense` | Switch debouncing, light sensor, dock state, derived events | Plain C++; light sensor on ADC1 ([HW-D1](#hw-d1--mcu-choice)) |
| `furby_ir` | Furby IR codec on top of raw timings | Plain C++; the codec is arithmetic, see [A3](#a3-the-infrared-protocol-decoded) |
| `furby_audio` | Microphone capture + DSP, speaker output, volume | Own DSP after [B3](#b3-audio-dsp-parameters); ESP8266Audio for RTTTL and diagnostic tones |
| `furby_assist` | Wyoming satellite server — TCP listener, discovery, event handling | New; reuses `furby_audio` for capture and playback |
| `furby_persona` | Mode state, reaction table, idle behaviour, speak() entry point | Plain C++ |
| `furby_link` | Control and event transport to Home Assistant | Per [ARCH-D4](#56-arch-d4--control-and-event-transport) — MQTT with discovery, recommended |

**Transport split.** `furby_assist` carries voice and announcements
([ARCH-D2](#54-arch-d2--home-assistant-voice-transport)); `furby_link` carries the controls and
events of [§10](#10-home-assistant-interface). They are separate connections to the same Home
Assistant and must not be conflated.

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

**Resolved 2026-09-07: ESP32 classic, WROOM-class module.**

Once [ARCH-D1](#53-arch-d1-resolution) removed ESPHome and
[HW-D7](#hw-d7--wake-word-strategy) moved wake word detection to the Home Assistant server, the
ESP32-S3's remaining advantages stopped being decisive:

| S3 advantage | Does it still matter here? |
| --- | --- |
| PSRAM | Barely. Wyoming streams PCM in chunks, so no complete audio file is ever buffered; the T-Watch runs the same audio stack in 520 KB of SRAM while also driving a display. |
| On-device wake word (`esp-sr` / microWakeWord) | No — it runs on the server instead, at no cost to the device. |
| Native USB (CDC + JTAG) | A genuine convenience: flashing and debugging without a USB-UART bridge, which is worth something for a device sewn into plush. OTA ([F-01](#f-01--base-node), [F-70](#f-70--update-and-recovery)) covers the normal case. |
| SIMD/DSP instructions | Irrelevant — a high-pass and a limiter at 16 kHz are rounding errors in CPU load. |
| Cleaner GPIO map | A small advantage; see the ADC2 trap below. |

**Decided on:** the classic parts are on hand and familiar, the legacy pin map in
[Appendix A1](#a1-legacy-pin-map) transfers unchanged, and the S3 no longer buys anything that
this design needs.

> **Not** decided on "the only chip that can keep the original audio parts" — see the correction
> in [HW-D3](#hw-d3--speaker-and-amplifier). That argument does not hold, and it should not be
> repeated.

**ADC2 trap.** On the ESP32 classic, ADC2 is unavailable while WiFi is active. The light sensor
([F-21](#f-21--light-sensor)) must therefore sit on **ADC1 (GPIO32–39)**, and GPIO34–39 are
input-only. This constrains the pin map in [HW-D6](#hw-d6--gpio-budget) and must be respected
from the first wiring diagram.

**Reopening clause:** if [HW-D2](#hw-d2--microphone) and [HW-D3](#hw-d3--speaker-and-amplifier)
both land on digital parts, the classic's remaining technical edge disappears entirely. The
decision would still stand on familiarity and stock, but it is then worth a deliberate
re-confirmation rather than an assumption.

#### HW-D2 — Microphone

| Option | Pros | Cons |
| --- | --- | --- |
| **Original electret + small preamp** (MAX9814 / MAX4466) | Keeps an original part. Preamp boards are tiny. | ESP32 classic **only**, custom firmware **only**. Noise floor of a 1998 electret is unknown. Needs a preamp because the original one is gone. |
| **PDM MEMS mic** (e.g. SPM1423, as in the T-Watch) | 2 pins only. Driver already written in the T-Watch project. Very small. | Original part removed. |
| **I²S MEMS mic** (INMP441 / ICS-43434) | Best noise performance. Works on every framework and both MCUs. | 3 pins. Original part removed. Board is ~15 × 18 mm. |

**Measurement task:** identify the electret's type and check whether it still works; record a
sample through a bench preamp and judge whether speech at ~1 m is usable. Judge it **twice**:
close-talk (push-to-talk) and far-field (always-on wake word) are different bars.

**Estimate, pending measurement (2026-09-07).** No bench access yet, so this is reasoning from
specifications, not a result:

| Question | Estimate | Confidence |
| --- | --- | --- |
| Original electret for **push-to-talk**? | Plausible. A typical electret capsule sits near −44 dBV/Pa, and the ageing failure mode is gradual loss of sensitivity rather than outright failure. Close-talk through a MAX9814 plus the DSP chain in [B3](#b3-audio-dsp-parameters) should carry it. | medium |
| Original electret for **always-on wake word**? | Probably not. Far-field is unforgiving, and the classic's internal ADC is the bottleneck — nominally 12 bit but realistically ~9–10 ENOB and non-linear, needing calibration. A 1998 toy capsule plus a cheap preamp plus a weak ADC is the wrong chain for continuous listening. An I²S MEMS part (~61–65 dB SNR) bypasses both the preamp and the ADC. | medium-high |

**Recommendation:** if [HW-D7](#hw-d7--wake-word-strategy)'s docked wake-word mode is wanted — and
it is — plan for an **I²S MEMS microphone** and treat the original electret as the pleasant
surprise rather than the baseline. Note that the T-Watch's gain table reaching **+42 dB** shows
that heavy gain plus DSP is normal in this class of device — see
[Appendix B](#b3-audio-dsp-parameters).

#### HW-D3 — Speaker and amplifier

| Option | Pros | Cons |
| --- | --- | --- |
| **Original speaker + internal DAC + small amp** (PAM8403 / LM4871) | Keeps the original transducer *and* the original signal path character. Frees three I²S pins. | ESP32 classic + custom firmware only. Internal DAC is 8-bit-ish quality. |
| **Original speaker + I²S amp** (MAX98357A) | Keeps the transducer; clean digital path; works on every framework and MCU. | One added module (~16 × 14 mm), 3 pins. |
| **New speaker + I²S amp** | Best audio quality. | Loses an original part; the Furby's speaker cavity constrains size anyway. |

> **Correction (2026-09-07).** Earlier drafts treated the internal DAC as the option that keeps
> the original audio "without an add-on module". That is wrong: a DAC pin sources a few
> milliamps and **cannot drive an 8 Ω speaker**, so this path needs an amplifier exactly like the
> I²S path does — and the original electret needs a preamplifier, because the original one left
> with the mainboard. **Module count is effectively the same in every variant (~2 small boards).**
> The internal DAC saves two GPIOs, not a module. This correction removed the main argument that
> [HW-D1](#hw-d1--mcu-choice) used to rest on.

**Measurement task:** measure the original speaker's DC resistance to infer impedance, then drive
it from a bench amplifier with a TTS sample. **Judge intelligibility, not fidelity** — a Furby is
allowed to sound like a toy, but the assistant's answers must be understandable.
*(This no longer gates [ARCH-D1](#53-arch-d1-resolution); it decides HW-D3 alone.)*

**Estimate, pending measurement (2026-09-07):**

| Question | Estimate | Confidence |
| --- | --- | --- |
| Keep the original transducer? | Yes. A ~36 mm mylar cone has nothing below its resonance, but 1–3 kHz — where consonants are discriminated — is its *best* region. Text-to-speech will sound thin and tinny and still be intelligible, which is exactly the criterion above. | high |
| Internal DAC or I²S amplifier? | **I²S amplifier.** The internal DAC is 8-bit, with DC offset and noise on top; speech without dithering is audibly grainy. Given the correction above it saves no module at all. | high |

**Recommendation:** keep the original transducer, drive it from a **MAX98357A-class I²S
amplifier**. This also keeps the door open if [HW-D2](#hw-d2--microphone) lands on a digital
microphone, since both then share one I²S clock domain.

#### HW-D4 — Motor driver

| Option | Notes |
| --- | --- |
| **DRV8833** | Small, 1.5 A per channel, built-in current limiting, fine for a toy gearmotor. Two PWM inputs (no separate enable). |
| **TB6612FNG** | Very common, PWM + two direction pins — matches the legacy wiring 1:1. |
| **Original H-bridge** | Was part of the removed mainboard. Not available. |

**Measurement task:** measure the motor's stall current and no-load current to size the driver
and the supply.

**Recommendation:** **TB6612FNG** — it matches the existing three-pin drive scheme
(PWM + forward + backward) from the legacy sketch, so [Appendix A](#a2-motor-drive-parameters)
transfers directly.

#### HW-D5 — Power

**Resolved 2026-09-07: Li-ion battery with a wireless (Qi) charging circuit. Operating model —
the Furby sits on its charging base most of the time.**

The motor's inrush current is the classic cause of ESP32 brownouts in this kind of build, and the
operating model changes what the battery is *for*:

- **The battery is primarily a peak buffer, not an energy store.** A Qi receiver in the baseline
  profile delivers 5 W; motor start or stall current plus WiFi transmit can exceed that. The
  battery absorbs exactly those peaks, which means it is **not optional even while docked** — it
  is the actual brownout protection. It also means capacity requirements are modest, so the cell
  can be small, which is what the space budget needs.
- **A separate motor rail with ≥ 470 µF of bulk capacitance at the driver stays mandatory**, from
  day one.
- **Two operating states, not one.** Docked, power is not the constraint and only the mechanical
  duty limit of [F-72](#f-72--motor-safety-limits) applies. Undocked, runtime becomes a quantity
  someone cares about — see [F-45](#f-45--volume-and-quiet-hours) and
  [F-72](#f-72--motor-safety-limits).
- **A dock / charge-state signal is required, not optional.** Without it the device can neither
  distinguish the two states nor trigger the `003-Low-Battery` phrase from
  [Appendix C1](#c1-system) sensibly. Budget 1–2 GPIOs in [HW-D6](#hw-d6--gpio-budget) and expose
  it via [F-63](#f-63--diagnostics).

**Open sub-question:** whether the charging circuit is a 5 W (BPP) or 15 W (EPP) receiver. This
sets the headroom the battery has to cover and should be settled with the part choice.

**Risk to design against — permanent charging.** A Li-ion cell held at charge-termination voltage
indefinitely, next to a Qi coil's waste heat, inside a closed plush enclosure, is the worst case
for cell ageing. The charger needs proper CC/CV termination and a recharge threshold rather than
an indefinite float. This is stated as a risk, not as a solved problem — the part choice is a
hardware decision to be made deliberately.

**Measurement tasks:** motor stall and start currents (from HW-D4); total idle draw with WiFi
active; **battery temperature under continuous charging inside the closed enclosure**.

**Mechanical consequence:** the resting position on the charging base fixes the coil alignment,
and therefore the pose the Furby normally sits in. An enclosure constraint, not a firmware one.

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
| Dock / charge state ([HW-D5](#hw-d5--power)) | 1–2 |
| **Total** | **16–22** |

**Constraints on top of the raw count:**

- **ADC1 only for the light sensor.** ADC2 is unusable while WiFi is active on the ESP32 classic,
  so [F-21](#f-21--light-sensor) must sit on GPIO32–39 — of which GPIO34–39 are input-only. See
  [HW-D1](#hw-d1--mcu-choice).
- **Shared I²S clocks.** If both microphone and amplifier are I²S, they can share BCLK and WS,
  so the pair costs about 4 pins rather than 6.
- **Space, not just pins.** The wireless charging circuit from [HW-D5](#hw-d5--power) competes
  for the same volume as any add-on board, which is a harder limit here than the pin count.

**Consequence:** very small boards (e.g. XIAO ESP32S3 at 21 × 17.5 mm) do **not** have enough
usable GPIOs. Either use a full WROOM module, or add an I²C port expander (PCF8574) for the four
slow switches, which brings the direct count down to ~14–20.

**Recommendation:** full WROOM-class module. Reserve the expander as the fallback if the
enclosure forces a smaller board.

#### HW-D7 — Wake word strategy

**Resolved 2026-09-07: two trigger modes, selected by dock state.**

| Dock state | Trigger |
| --- | --- |
| **Docked** | Continuous audio stream to Home Assistant with the pipeline started at `wake` stage; openWakeWord detects on the server. Hands-free. |
| **Undocked** | Push-to-talk on the tummy switch only. No streaming, no wake word. |

| Option | Assessment |
| --- | --- |
| On-device (`micro_wake_word` / `esp-sr`) | **Excluded** by [ARCH-D1](#53-arch-d1-resolution) and [HW-D1](#hw-d1--mcu-choice) — it needs ESPHome in practice and an ESP32-S3. |
| **Home Assistant side** *(chosen for docked operation)* | Works on any MCU: the device only streams 16 kHz PCM, which costs it no more than ordinary listening — no PSRAM, no on-device model. The price is continuous streaming, which is why it is limited to the docked state where power and bandwidth are free. |
| **Push-to-talk on the tummy switch** *(chosen for undocked operation, and built first)* | Zero extra cost, and *pressing the Furby's tummy to talk to it* is arguably the most Furby-like interaction of the three. It is also a prerequisite for testing everything else. |

**Wake word phrase: "Hey Furby".** Custom models are the normal path, not an exception — Home
Assistant publishes a training notebook, Piper generates the synthetic clips, and background
noise and room reverb are mixed in. The model lives on the server, so changing the phrase is a
file, not a reflash.

**Not "Ok Furby".** The household already runs "Ok Nabu" in the same room. The two phrases share
the entire "Ok" prefix, the medial /b/, the syllable count and the stress pattern, leaving two
vowels to tell them apart — a strong recipe for cross-triggering, where saying "Ok Nabu" also
wakes the Furby. Dropping the shared prefix is the cheapest large improvement. A Furbish-flavoured
phrase would be even more distinctive, if a more distinctive one is wanted later.

**Verification task:** with both models enabled, say "Ok Nabu" twenty times in the same room and
count how often the Furby reacts. Do this before committing to the phrase — it is cheap, and the
phrase is trivially replaceable.

**Dependency:** wake-word quality rides on the microphone, not on the MCU. Far-field detection is
the demanding case in [HW-D2](#hw-d2--microphone), and it is the reason that decision leans
towards a MEMS part.

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
| **M0** | Decisions & measurements | ARCH-D1/D2/D3 ✅, HW-D1/D5/D7 ✅; remaining: ARCH-D4, HW-D2, HW-D3, HW-D4, HW-D6, HW-D8 | All decisions resolved and recorded in §13 |
| **M1** | Foundation | F-01, F-02 | Device boots, is reachable, can be updated over the air |
| **M2** | Motion | F-10, F-11, F-20, F-21, F-22 | The Furby homes, holds named poses, and reacts physically to touch |
| **M3** | Audio & voice | F-40, F-41, F-42, F-45 | The Furby is a Wyoming satellite in Home Assistant and a full voice interaction works end to end, push-to-talk and docked wake word |
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
- **Description:** a documented repository layout, a reproducible PlatformIO build, secrets kept
  out of version control, and a CI job that at minimum compiles the firmware on every push.
  `platformio.ini` must pin the `pioarduino` platform fork explicitly — see
  [ARCH-D3](#55-arch-d3--framework-flavour-and-toolchain).
- **Acceptance:** a clean checkout builds; CI fails on a deliberately broken build; no
  credentials are present in the repository.
- **Depends on:** ARCH-D1, ARCH-D3.
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
- **Implementation note:** plain C++ with an interrupt-driven encoder counter; the legacy sketch's
  LEDC parameters and the direction-change guard in
  [Appendix A2](#a2-motor-drive-parameters) are the starting point.
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
- **Description:** the output path from an audio stream to the speaker, including volume control
  with minimum/maximum limits, mute, and ducking so a spoken answer can talk over other audio.
  Must be able to play a stream received from Home Assistant (a TTS response) as well as a
  locally generated tone for diagnostics.
- **Acceptance:** a TTS response plays intelligibly at a normal listening distance; volume
  changes take effect immediately; a diagnostic tone confirms the path without any network.
- **Depends on:** F-01, HW-D1, HW-D3.
- **Implementation note:** assistant answers arrive as **raw PCM chunks** over Wyoming
  ([ARCH-D2](#54-arch-d2--home-assistant-voice-transport)), so **no MP3 or WAV decoder is needed**
  for them — the chunks go straight to the output. ESP8266Audio remains useful for RTTTL jingles
  and diagnostic tones ([Appendix B4](#b4-microphone-and-speaker-drivers)).
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
  transcript, the answer text, and the spoken answer → play it. Two triggers, selected by dock
  state ([HW-D7](#hw-d7--wake-word-strategy)): **docked**, the device streams continuously and
  Home Assistant detects the wake word; **undocked**, push-to-talk on the tummy switch only.
  Push-to-talk is built first — it is a prerequisite for testing everything else. The pipeline
  must handle errors visibly (see [F-44](#f-44--assistant-state-feedback)) and time out rather
  than hang. A conversation timeout resets the conversation context.
- **Acceptance:** press tummy → speak a command → Home Assistant executes it → the Furby speaks
  the answer; **while docked, saying the wake word starts the same interaction hands-free, and
  while undocked it does not**; a network failure mid-request produces a visible error state and
  returns to idle; the device recovers without a reboot.
- **Depends on:** F-40, F-41, F-20, F-63 (dock state).
- **Implementation note:** the device is a **Wyoming satellite**
  ([ARCH-D2](#54-arch-d2--home-assistant-voice-transport)): it listens on a TCP port, Home
  Assistant connects to it, and it is discovered by zeroconf or added by host and port (see
  [§13.2](#132-open-questions)). It requests pipeline runs and handles the audio and pipeline
  events of the protocol. No access token is stored on the device. The T-Watch assist client is
  *not* the reference here; its capture and playback layers
  ([B3](#b3-audio-dsp-parameters), [B4](#b4-microphone-and-speaker-drivers)) still are.
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
  The schedule is not the only gate: **dock state matters too**
  ([HW-D5](#hw-d5--power)). Docked, power is free and only the mechanical duty limit of
  [F-72](#f-72--motor-safety-limits) applies. Undocked, unprompted output should also be
  restrained to protect runtime.
- **Acceptance:** an event-triggered phrase at 23:00 does not play; a direct voice request at
  23:00 is answered quietly; volume steps correctly at the schedule boundaries; the schedule is
  configurable from Home Assistant; undocked, unprompted output is measurably rarer than docked.
- **Depends on:** F-01, F-40, F-63 (dock state).
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
- **Depends on:** F-22, F-42, ARCH-D4.
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
  **Realised as `assist_satellite.announce`** ([ARCH-D2](#54-arch-d2--home-assistant-voice-transport)):
  Home Assistant renders the text with the mode's voice ([F-43](#f-43--mode-dependent-voice)) and
  streams raw PCM; the device plays and animates it. Because `announce` is a **push**, the
  queueing, priority and quiet-hours policy have to be enforced **on the device** — nothing on the
  Home Assistant side will do it.
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
  level, current mode and volume, plus **dock state, charge state and battery voltage**
  ([HW-D5](#hw-d5--power)). Dock state is not merely diagnostic — it selects the trigger mode in
  [F-42](#f-42--voice-assistant-pipeline) and gates unprompted output in
  [F-45](#f-45--volume-and-quiet-hours) — and battery voltage is what triggers the
  `003-Low-Battery` phrase from [Appendix C1](#c1-system).
- **Acceptance:** a Home Assistant dashboard shows all of them; a deliberately induced motor
  stall increments the fault counter visibly; the homing status correctly reads "not homed"
  after a fault; lifting the Furby off its base changes the dock state within a second.
- **Depends on:** F-10, F-42, ARCH-D4.
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
  The budget is **mechanical, and applies in both dock states**. Undocked, a second and stricter
  budget applies on top of it to protect runtime ([HW-D5](#hw-d5--power)) — the mechanical limit
  is never relaxed just because the Furby is on its charger.
- **Acceptance:** a five-minute continuous talk animation stays within the duty budget by
  thinning out movement rather than running the motor constantly; a stall stops the motor
  within the timeout; the fault is visible in diagnostics and clears on a successful re-home;
  the mechanical limit is identical docked and undocked.
- **Depends on:** F-10, F-63 (dock state).
- **Milestone:** M6.

---

## 10. Home Assistant Interface

This contract is carried by **two separate transports**, and it matters which one carries what:

- **Voice and announcements** go over the Wyoming satellite connection
  ([ARCH-D2](#54-arch-d2--home-assistant-voice-transport)). That gives an `assist_satellite`
  entity and a wake-word select — and nothing below.
- **Everything below** — every control in §10.1 and every event in §10.2 — is carried by
  `furby_link` over the transport still to be chosen in
  [ARCH-D4](#56-arch-d4--control-and-event-transport).

### 10.1 Exposed controls

| Control | Type | Purpose |
| --- | --- | --- |
| Mode | select (`normal` / `cute` / `evil`) | Read and set the personality |
| Volume | number (%) | Current speaking volume |
| Eyes | light | Colour and brightness |
| Quiet hours enabled | switch | Master switch for the schedule |
| Play animation | action (name) | Trigger a named animation |
| Speak | `assist_satellite.announce` | The [F-61](#f-61--speak-interface) entry point — the one control that *does* come from the Wyoming side |
| Send IR message | action (message name) | [F-31](#f-31--infrared-transmit) |
| Re-home | action | Force a homing cycle |
| Dock state | binary sensor | Docked / undocked ([HW-D5](#hw-d5--power)), drives [F-42](#f-42--voice-assistant-pipeline) and [F-45](#f-45--volume-and-quiet-hours) |

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
| `furby.dock_changed` | Placed on or lifted off the charging base (with `docked`) |

### 10.3 Naming conventions

- Device/host name: `d03n3rFurby` (carried over from the legacy project). The Wyoming satellite
  advertises itself under the same name, so both transports present one device.
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

```
esp32-furby/
├── README.md
├── docs/
│   ├── FSD.md                  ← this document
│   ├── hardware.md             ← pin map, wiring, measurements (from M0)
│   └── decisions/              ← one file per resolved decision, if they grow
├── firmware/                   ← PlatformIO project (ARCH-D3)
│   ├── platformio.ini          ← pins the pioarduino platform fork
│   ├── include/
│   ├── src/                    ← one directory per module from §6.3
│   ├── lib/
│   └── config.example.h        ← template; the real config.h is git-ignored
└── homeassistant/
    ├── packages/               ← phrase catalogue, event automations
    └── blueprints/
```

Credentials live only in the ignored `firmware/config.h`
([NFR-8](#11-non-functional-requirements)). The legacy `config.h` in the project history was
deliberately emptied before sharing and stays that way.

---

## 13. Open Questions & Decision Log

### 13.1 Open decisions

| ID | Decision | Blocks | Status |
| --- | --- | --- | --- |
| [ARCH-D4](#56-arch-d4--control-and-event-transport) | Control and event transport | F-50, F-62, F-63, §10 | Open — MQTT with HA discovery recommended; resolve before M4 |
| [HW-D2](#hw-d2--microphone) | Original electret vs. MEMS microphone | F-41 | Open — measurement pending; estimate favours MEMS given HW-D7 |
| [HW-D3](#hw-d3--speaker-and-amplifier) | Speaker and amplifier path | F-40 | Open — measurement pending; estimate favours original transducer + I²S amp |
| [HW-D4](#hw-d4--motor-driver) | Motor driver | F-10 | Open — leaning TB6612FNG, needs stall current |
| [HW-D6](#hw-d6--gpio-budget) | GPIO budget / port expander | All | Open — depends on enclosure |
| [HW-D8](#hw-d8--eye-leds) | Eye LED type | F-22 | Open — confirm what is already wired |

### 13.2 Open questions

1. **`-Serious` semantics.** The catalogue pairs several phrases with a `-Serious` variant. The
   assumed reading is *"the event repeated or persisted, so escalate"*. Some entries
   (`AirQuality-Serious`, `ClimateWarning-Serious`) instead read like a *reply to an announcement
   another system just made*. Which is intended — or is it both, depending on the entry?
2. **Cam position table.** The named poses in [F-11](#f-11--named-poses-and-animation-sequencer)
   must be calibrated against the actual gearbox. Is a reference table from the original
   available, or do we measure all of them empirically during M2?
3. **Eye LED wiring.** What is already installed — addressable or discrete? ([HW-D8](#hw-d8--eye-leds))
4. **Second Furby.** Is one available for testing [F-30](#f-30--infrared-receive) /
   [F-31](#f-31--infrared-transmit), or do we test against recorded codes only?
5. **Meaning of IR messages #2 and #8.** Their frames are known and reproducible
   ([A3](#a3-the-infrared-protocol-decoded)), but not what they say. Two ways to find out:
   transmit them at a real Furby and watch, or mine the original source listing
   ([A8](#a8-sources)) once it is reachable.
6. **Satellite discovery.** How does Home Assistant find the Wyoming satellite
   ([ARCH-D2](#54-arch-d2--home-assistant-voice-transport)) — does the device announce itself over
   zeroconf, or is it added manually by host and port? The manual route is simpler to build and
   needs a fixed address; zeroconf is friendlier and survives a DHCP change.
7. **Wake word cross-triggering.** Does "Hey Furby" false-trigger on "Ok Nabu" in the same room?
   The verification task is in [HW-D7](#hw-d7--wake-word-strategy): say "Ok Nabu" twenty times
   with both models enabled and count. Run it before settling on the phrase.

### 13.3 Resolved decisions

| Decision | Resolution | Date |
| --- | --- | --- |
| Speech source | All speech is live Home Assistant TTS; no pre-rendered on-device samples | 2026-09-04 |
| Behaviour depth | Reactions, animations and modes; **no** needs/mood simulation | 2026-09-04 |
| Motion sensing | Original gearbox retained, using **both** the optical encoder and the sync switch | 2026-09-04 |
| Framework stance | ESPHome preferred but not mandatory; reusing original parts ranks higher | 2026-09-04 |
| ↳ [ARCH-D1](#53-arch-d1-resolution) | Applied that stance and resolved it: **custom firmware**, ESPHome dropped. Space budget and audio stack decided it; the HW-D3 gate was removed because path B is agnostic to the audio path | 2026-09-07 |
| Documentation language | English, except the German phrase content | 2026-09-04 |
| [ARCH-D2](#54-arch-d2--home-assistant-voice-transport) | **Wyoming satellite** as the voice transport, not the raw Assist WebSocket — it sits above the `assist_satellite` feature ceiling, needs no token on the device, and delivers TTS as raw PCM | 2026-09-07 |
| [ARCH-D3](#55-arch-d3--framework-flavour-and-toolchain) | **PlatformIO + Arduino-ESP32 3.x via the `pioarduino` platform fork** | 2026-09-07 |
| [HW-D1](#hw-d1--mcu-choice) | **ESP32 classic**, WROOM-class module — on hand, familiar, legacy pin map transfers, and the S3 buys nothing once wake word runs server-side | 2026-09-07 |
| [HW-D5](#hw-d5--power) | **Li-ion + wireless charging, mostly docked.** The battery is a peak buffer for motor inrush, not an energy store | 2026-09-07 |
| ↳ Battery operation | Yes — this answers the former open question 4 | 2026-09-07 |
| [HW-D7](#hw-d7--wake-word-strategy) | **Two trigger modes by dock state:** docked → continuous stream with server-side wake word; undocked → push-to-talk on the tummy switch | 2026-09-07 |
| ↳ Wake word phrase | **"Hey Furby"**, not "Ok Furby" — the shared "Ok" prefix collides with "Ok Nabu" in the same room. This answers the former open question 5, subject to the cross-trigger test | 2026-09-07 |

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
| [ESPHome documentation](https://esphome.io) (`esphome/esphome-docs`) | The framework constraints in [§5.1](#51-the-core-tension-arch-d1) — notably that the internal microphone ADC and internal DAC are no longer supported. Historical: ESPHome was dropped in [ARCH-D1](#53-arch-d1-resolution). |
| [`home-assistant/core`](https://github.com/home-assistant/core) — `components/assist_pipeline/websocket_api.py` and `components/wyoming/assist_satellite.py` (read 2026-09-07) | The evidence behind [ARCH-D2](#54-arch-d2--home-assistant-voice-transport): `assist_pipeline/run` is alive and undeprecated, and the Wyoming integration builds a full `assist_satellite` entity with `announce` implemented. |
| [`pioarduino/platform-espressif32`](https://github.com/pioarduino/platform-espressif32) | The toolchain constraint in [ARCH-D3](#55-arch-d3--framework-flavour-and-toolchain): Arduino-ESP32 3.x on ESP-IDF 5.5 comes from this fork, not from the official PlatformIO platform. |
| [Wake words for Assist](https://www.home-assistant.io/voice_control/create_wake_word/) | The custom wake word training path in [HW-D7](#hw-d7--wake-word-strategy) — Piper-generated synthetic clips, trained in a published notebook. |

---

## Appendix B — Reusable from the T-Watch project

Source: `d03n3rfr1tz3/TTGO.T-Watch.2020`. [ARCH-D1](#53-arch-d1-resolution) landed on custom
firmware, so this appendix applies.

| Section | Status |
| --- | --- |
| [B1](#b1-home-assistant-assist-client), [B2](#b2-audio-framing) | **Superseded** by [ARCH-D2](#54-arch-d2--home-assistant-voice-transport) — the Wyoming satellite replaces the WebSocket assist client and its framing. Kept as the reference should the raw pipeline path ever be needed again. |
| [B3](#b3-audio-dsp-parameters), [B4](#b4-microphone-and-speaker-drivers) | **Fully applicable** — transport-agnostic, and the larger part of the work. |

### B1. Home Assistant assist client

> **Superseded by [ARCH-D2](#54-arch-d2--home-assistant-voice-transport).** Retained as reference.

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

> **Superseded by [ARCH-D2](#54-arch-d2--home-assistant-voice-transport)** — Wyoming carries its
> own framing. Retained as reference.

Audio is sent as binary WebSocket frames: a **handler byte** followed by **signed 16-bit
little-endian mono PCM**, 1024 bytes per frame. A lone handler byte signals end-of-audio.
The sample format itself — signed 16-bit little-endian mono at 16 kHz — is unchanged under
Wyoming; only the envelope differs.

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
  for MP3, WAV and RTTTL, and sources for SPIFFS, PROGMEM and RAM. Under
  [ARCH-D2](#54-arch-d2--home-assistant-voice-transport) the MP3 and WAV generators are no longer
  needed for assistant answers — those arrive as raw PCM — but **RTTTL** remains a cheap way to
  give the Furby jingles and diagnostic tones without any audio files.

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
