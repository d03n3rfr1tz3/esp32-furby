# Tests

Run the host suite with:

```sh
pio test --project-dir firmware -e native
```

It needs no ESP32 attached, and CI runs it on every push. The framework is Unity, which
PlatformIO supplies.

## Layout

One directory per suite, each named `test_*`, each with its own `main()`. PlatformIO builds
and runs them separately, so a crash in one suite does not hide the others.

## The convention that keeps tests possible

**Hardware-independent logic goes into `lib/<module>/` and does not include `Arduino.h`.**
A native test links such a module directly. `src/` holds only the thin layer that actually
touches a pin, a peripheral or the network — the part a host cannot run.

Concretely, for the modules of FSD §6.3: the IR codec is arithmetic, the pose table is a
lookup, debouncing and the light-sensor thresholds are state machines over timestamped
samples, and the quiet-hours schedule is a function of the clock. All of that belongs in
`lib/` and is testable here. Reading the pin does not.

Where a module must talk to hardware, the test seam is an injected interface — the module
takes something it can call, and the test passes a fake. That is a design constraint on the
module, which is why it is written down before the first module exists.

## On-device tests

Some things only prove themselves on the real Furby: encoder counts, homing repeatability,
stall detection. Those are the acceptance criteria in the FSD and are checked by hand on the
bench, not here. If they ever become automated, they get their own environment — the `native`
suite stays host-only.
