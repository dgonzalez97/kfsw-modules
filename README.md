# K-FSW Modules

Reusable K-FSW device, subsystem, and mission-specific modules for Zephyr
flight and ground compositions.

## Repository boundary

This repository owns reusable functionality that does not naturally belong to
the generic K-FSW platform, service, or communications repositories. Possible
future modules include UHF radios, GNSS receivers, ADCS or EPS devices, and
payload devices. These are examples, not implemented capabilities.

The generic boundaries remain:

- `kfsw-platform`: Zephyr-facing mechanisms such as monotonic time, storage,
  reset cause, and future watchdog support;
- `kfsw-services`: reusable services such as logging, PARAM, persistence, and
  file transfer; and
- `kfsw-comms`: libcsp lifecycle and generic transports such as KISS and
  future CAN/CFP.

A device using a generic mechanism does not take ownership of that mechanism.
For example, a future Holybro implementation may use KISS without moving KISS
out of `kfsw-comms`.

## Build and composition model

`zephyr/module.yml` makes this repository a normal Zephyr module discovered
through the K-FSW west manifest. The root `Kconfig` and `CMakeLists.txt` are
extension points for real modules as they are introduced.

Each future module is selected independently at compile time. Its directory
can contribute sources with normal conditional CMake subdirectories and can
select one concrete implementation through Kconfig. Repository presence alone
does not enable a feature or create runtime behavior. There is no runtime
plugin manager or central source registry.

The foundation intentionally has no public include directory or foundation
API, because there is no runtime abstraction to expose yet.

## Module ownership pattern

A module may own its public interface, parameters and their semantics, health
reporting, concrete implementations, optional shell integration, and tests.
Only create the directories that a real module needs.

For example, a future radio-UHF module may follow this conceptual pattern:

```text
radio-uhf/
  interface
  param_uhf
  health
  holybro/
    implementation
```

This is a future ownership example only. No radio-UHF or Holybro module exists
in this foundation. For radio-UHF, `param_uhf` is the parameter-owning unit: it
defines the UHF parameters and exposes their definition set. `health` owns the
module's bounded health/status interface.

## Parameter ownership

An owning module defines its static `struct kfsw_param_definition` entries and
groups them in an exposed `struct kfsw_param_definition_set`. The executable
composition adds that set when the module is enabled.

This keeps definition and semantic ownership in the module while preserving
the existing boundaries:

- `KFSW_PARAM` owns aggregation, lookup, get/set, validation execution, and
  enumeration;
- `KFSW_PARAM_PERSISTENCE` owns the KPAR persistence mechanism; and
- `KFSW_PARAM_CSP` owns remote CSP exposure.

Contributing a module definition set must not require editing `parameter.c`,
adding a PARAM-to-module dependency, or making the module aware of CSP. No fake
parameters are included in this foundation.
