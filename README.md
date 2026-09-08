# esp32-furby

Furby (1998) refurbished with an ESP32 running custom firmware (PlatformIO / Arduino-ESP32),
speaking to Home Assistant as a Wyoming voice satellite.

The original mainboard is replaced by an ESP32 that drives the original motor, gearbox, sensors
and switches. The goal is a subset of the original Furby's animatronic behaviour combined with a
Home Assistant voice assistant, plus reactive phrases on prepared smart-home events in three
personality modes (normal, cute, evil).

## Status

Planning. No firmware yet — the current deliverable is the specification. The framework and
transport decisions are settled; several hardware decisions still wait on bench measurements.

## Documentation

- **[docs/FSD.md](docs/FSD.md)** — the Functional Specification Document. It holds every planned
  feature, the open hardware and architecture decisions, the phrase catalogue, and the knowledge
  salvaged from the earlier Arduino sketch. Development proceeds one feature at a time from this
  document.
