# ESP32 Furby

## Project goal

A Furby from 1998 is being refurbished: its original mainboard is removed and replaced by an
ESP32, which drives the original motor, gearbox, sensors and switches. The goal is a subset of
the original Furby's animatronic behaviour, combined with a Home Assistant voice assistant and
reactive phrases on prepared smart-home events, in three personality modes (normal, cute, evil).

Priority order when in doubt: **reuse original Furby parts** > convenience of a framework.
ESPHome is preferred but not mandatory — see `ARCH-D1` in the FSD.

---

## Important documents

- **`docs/FSD.md`** — Functional Specification Document. The source of truth for *what* gets
  built: every planned feature with acceptance criteria, the open architecture and hardware
  decisions, the phrase catalogue, and the knowledge salvaged from the earlier Arduino sketch.
  Read it before proposing anything.
- **`docs/hardware.md`** — pin map, wiring and measurements taken on the real device.
  *(To be created in milestone M0.)*
- **`README.md`** — short project overview and entry point.

---

## Working agreements

### Language

- **Conversation with the user: German.**
- **Everything inside the repository: English** — documentation, code, comments, commit
  messages, PR descriptions.
- Exception: the German phrase catalogue in the FSD is content, not documentation. It stays
  German.

### Development flow

- Work is **agile and incremental: one feature per session**, sometimes less. Plan it, implement
  it, test it on real hardware.
- The FSD drives the work. Pick a feature by its ID (`F-nn`), and keep its acceptance criteria
  in view — a feature is not done until they are met.
- When a session changes what the project will do, **update the FSD in the same commit**. The
  FSD is meant to live, not to rot.
- Decisions get stable IDs (`ARCH-Dn`, `HW-Dn`) and are resolved in place in the FSD's decision
  log. Never silently drop a decision.

### Hardware reality

- This is a physical project with a 28-year-old gearbox. **Do not guess hardware facts.** If
  something depends on an impedance, a current draw, a pin assignment or a cam angle, say what
  needs to be measured rather than assuming a plausible value.
- Several decisions in the FSD are explicitly gated on measurements. Respect the gate — do not
  pre-empt a decision that is waiting on a number from the bench.
- Motor safety is not optional. Run-time limits, duty-cycle budget and stall detection protect
  irreplaceable original mechanics.

### Git and commits

- Develop on the branch assigned for the session; create it if it does not exist.
- **Commit messages are English**, written in the imperative, with a body explaining the *why*
  when the change is not self-evident.
- **Never open a pull request unless explicitly asked.**
- Never force-push a branch that someone else may have checked out.

### Secrets

- No credentials, tokens, WiFi passwords or Home Assistant access tokens in the repository —
  they belong in an ignored local configuration file.
- The legacy `config.h` in the project history was deliberately emptied before sharing. Keep it
  that way.
