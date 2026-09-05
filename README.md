# K-FSW Modules

Reusable K-FSW device, subsystem, and mission-specific modules for Zephyr
flight and ground compositions.

## Repository boundary

This repository owns reusable functionality that does not naturally belong to
the generic K-FSW platform, service, or communications repositories. Current
implementations are the `radio-uhf` equipment module and the deliberately small
`boton_test` ownership example. Possible future modules include GNSS receivers,
ADCS or EPS devices, and payload devices; those examples are not implemented
capabilities.

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

## boton_test reference module

`boton_test` demonstrates the complete boundary for a small stateful hardware
module without taking ownership of generic mechanisms. Its developer-facing
logical table is named `hw_test`; table ID `67` is reserved for the following
generic Housekeeping integration:

```text
composition-chosen button -> edge ISR -> delayable work ---+
composition-chosen LEDs <-> owner LED setter <-> shell/PARAM +-> owner state
                                                            |        |
                                                            |        +-> typed status API
                                                            |                  |
                                                            +------------------+-> future HK table 67
```

The reusable source resolves `kfsw,boton-test-button`; it contains no board,
MCU, GPIO controller, or pin name. A target overlay maps that chosen phandle to
an existing GPIO node and its devicetree flags define active-low or active-high
polarity. The ISR only reschedules one static delayable item on Zephyr's system
workqueue. After the configurable debounce interval, work reads the logical
level. One stable released-to-pressed transition counts, holding does not
recount, and a stable release rearms the next press. Initialization is
serialized and schedules the same debounced sample after interrupts are
enabled, reconciling any transition that occurred during GPIO setup.

The module owns `press_count`, `last_press_s`, and independent green, blue, and
red LED booleans. All start at zero/off on every boot and are neither
persistent nor dynamically allocated. `press_count`
saturates at `UINT32_MAX`; accepted presses still update `last_press_s` after
saturation. Monotonic milliseconds from `kfsw-platform` are divided by 1000
with floor semantics, and the 32-bit seconds value also saturates rather than
wrapping after approximately 136 years.

`kfsw_boton_test_get_status()` copies all five fields under one short mutex so a
future Housekeeping collector can consume a consistent typed snapshot without
GPIO access or PARAM lookup. The module creates no thread and allocates no
memory dynamically. PARAM/CSP provides generic remote observation but is not
called by the button ISR or work handler.

The current PARAM definition model reads raw owner-backed scalars rather than
calling an owner getter. Each button entry is therefore an independent,
naturally aligned 32-bit view; it is not a coherent two-field snapshot and on
the tested targets relies on single-copy 32-bit access rather than a shared
formal C synchronization primitive. The typed API is the synchronized
interface for multi-field consumers. A future generic PARAM owner-read
callback would close that service-level limitation without duplicating state.

The first physical mapping is the NUCLEO-L496ZG blue USER button plus its three
independent user LEDs: `led0`/LD1 green, `led1`/LD2 blue, and `led2`/LD3 red.
The reusable module resolves only composition-selected nodes and uses their
Devicetree GPIO polarity flags. Native state, PARAM, saturation, shell, and
GPIO-emulator tests do not require that board. Physical bench evidence remains
a separate, user-driven acceptance step.

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
not create empty parameter or health subsystems. `boton-test` keeps its owner
state/API and GPIO adapter separate so native tests can exercise semantics
without a physical input.

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
adding a PARAM-to-module dependency, or making the module aware of CSP.

K-FSW uses one global 16-bit parameter-ID namespace. IDs are assigned as the
lowest unused value across production and reserved test definitions and are
never recycled. Existing assignments are:

| ID | Definition | Owner/status |
| --- | --- | --- |
| 0 | `node_id` | application composition |
| 1 | `log_level` | logging service |
| 2–5 | test fixtures | reserved even when disabled |
| 6 | `press_count` | `boton_test`, read-only, non-persistent |
| 7 | `last_press_s` | `boton_test`, read-only, non-persistent |
| 8 | `led_green` | `boton_test`, writable boolean, non-persistent |
| 9 | `led_blue` | `boton_test`, writable boolean, non-persistent |
| 10 | `led_red` | `boton_test`, writable boolean, non-persistent |

The registry and review policy keep assignments stable; PARAM aggregation
rejects any duplicate ID or name with `-EEXIST` as the executable collision
guard. Numeric allocation is central, while each actual definition remains in
its semantic owner.
