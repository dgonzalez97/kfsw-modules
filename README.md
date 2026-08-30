# K-FSW Modules

Reusable K-FSW device, subsystem, and mission-specific modules for Zephyr
flight and ground compositions.

## Repository boundary

This repository owns reusable functionality that does not naturally belong to
the generic K-FSW platform, service, or communications repositories. Its first
module is `radio-uhf`, with Holybro SiK as the first compile-time-selected
implementation. Possible future modules include GNSS receivers, ADCS or EPS
devices, and payload devices; those examples are not implemented capabilities.

The generic boundaries remain:

- `kfsw-platform`: Zephyr-facing mechanisms such as monotonic time, storage,
  reset cause, and future watchdog support;
- `kfsw-services`: reusable services such as logging, PARAM, persistence, and
  file transfer; and
- `kfsw-comms`: libcsp lifecycle and generic transports such as KISS and
  future CAN/CFP.

A device using a generic mechanism does not take ownership of that mechanism.
Holybro uses the existing transparent serial path without moving UART, KISS,
CSP, routing, or packet ownership out of `kfsw-comms`.

## Build and composition model

`zephyr/module.yml` makes this repository a normal Zephyr module discovered
through the K-FSW west manifest. The root `Kconfig` and `CMakeLists.txt` are
extension points for real modules as they are introduced.

Each module is selected independently at compile time. Its directory
contributes sources with normal conditional CMake subdirectories and selects
one concrete implementation through Kconfig. Repository presence alone does
not enable a feature or create runtime behavior. There is no runtime plugin
manager or central source registry.

## radio-uhf and Holybro SiK

The current module tree is deliberately small:

```text
radio-uhf/
├── include/kfsw/modules/radio_uhf.h
├── src/
│   ├── radio_uhf.c
│   └── radio_uhf_internal.h
└── holybro/src/holybro.c
```

The public API returns the selected implementation identity, expected hardware
identity, expected transparent-serial baud/flow-control contract, whether live
hardware status is available, and the RF-link state. Holybro currently has no
safe live status query, so hardware status is unavailable and RF link remains
`unknown`. The expected values are build-time composition facts, not hardware
readback.

`CONFIG_KFSW_RADIO_UHF` enables the module and the implementation choice selects
`CONFIG_KFSW_RADIO_UHF_HOLYBRO`. The expected serial baud defaults to 57600.
The target devicetree still owns the actual UART and pin configuration, and
`kfsw-comms` still owns the UART/KISS data path.

The optional `uhf status` shell diagnostic reports this same bounded snapshot.
It does not duplicate `csp info`, interfaces, routes, or UART counters.

No UHF parameters are currently exported. In particular, the module does not
offer writable TX-power, network-ID, or air-rate values because it does not yet
apply them to hardware. Adding values that accept writes but do nothing would
violate parameter ownership semantics.

General SiK AT control is also deferred. Entering command mode would interrupt
the live CSP/KISS serial path and requires an explicit arbitration design. The
module never executes `AT&W`, `AT&F`, or `ATS...`, and it does not alter the
verified `MAVLINK=1` setting.

## Module ownership pattern

A module may own its public interface, parameters and their semantics, health
reporting, concrete implementations, optional shell integration, and tests.
Only create the directories that a real module needs.

For example, a module may follow this conceptual ownership pattern:

```text
radio-uhf/
  interface
  param_uhf
  health
  holybro/
    implementation
```

Only directories backed by implemented behavior are created. `radio-uhf`
currently needs interface/status and Holybro implementation sources; it does
not create empty parameter or health subsystems.

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
