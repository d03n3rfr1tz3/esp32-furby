# esp32-furby

Furby (1998) refurbished with an ESP32 running custom firmware (PlatformIO / Arduino-ESP32),
speaking to Home Assistant as a Wyoming voice satellite.

The original mainboard is replaced by an ESP32 that drives the original motor, gearbox, sensors
and switches. The goal is a subset of the original Furby's animatronic behaviour combined with a
Home Assistant voice assistant, plus reactive phrases on prepared smart-home events in three
personality modes (normal, cute, evil).

## Status

Milestone M1 has started. The firmware is a build skeleton: it boots, reports the chip and
its version, and drives no hardware yet — the pin map is a product of the M0 bench
measurements that are still outstanding. The framework and transport decisions are settled;
several hardware decisions still wait on those measurements.

## Building

The firmware is a PlatformIO project. `platformio.ini` pins the `pioarduino` platform fork,
which is what provides the Arduino-ESP32 3.x core.

```sh
cp firmware/config.example.h firmware/config.h   # then fill it in
pio run --project-dir firmware                   # build
pio run --project-dir firmware --target upload   # flash over USB
pio test --project-dir firmware -e native        # run the unit tests on the host
```

`firmware/config.h` holds every credential and is git-ignored. It never gets committed.

The unit tests need no ESP32 attached; CI runs them on every push alongside the build.
[firmware/test/README.md](firmware/test/README.md) explains the layout and the design
convention that keeps logic testable off the device.

## Documentation

- **[docs/hardware.md](docs/hardware.md)** — the pin map, the installed parts and the
  measurements taken on the real device. Anything the FSD marks "to be measured" is answered
  here.
- **[docs/FSD.md](docs/FSD.md)** — the Functional Specification Document. It holds every planned
  feature, the open hardware and architecture decisions, the phrase catalogue, and the knowledge
  salvaged from the earlier Arduino sketch. Development proceeds one feature at a time from this
  document.
