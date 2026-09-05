# K-FSW Modules

Device and subsystem modules for K-FSW compositions — the code that knows about
a particular piece of hardware, as opposed to the generic mechanisms it runs
on.

Two exist today: `radio-uhf`, a UHF equipment module with a Holybro SiK
implementation, and `boton-test`, a small worked example of what owning
hardware looks like. GNSS receivers, ADCS or EPS devices and payloads would
belong here too; none of them are written.

## What belongs here

A module owns hardware. It does not own the mechanisms it uses to reach that
hardware:

- `kfsw-platform` owns Zephyr-facing mechanisms — time, storage, reset cause,
  watchdog;
- `kfsw-services` owns reusable services — logging, parameters, persistence,
  files, events, commands, health, firmware update;
- `kfsw-comms` owns libcsp and the transports under it.

Using a mechanism is not taking it over. The Holybro radio moves bytes over a
transparent serial link, and doing so leaves UART, KISS, CSP, routing and
packet ownership exactly where they were.

`zephyr/module.yml` makes this repository a normal Zephyr module found through
the west manifest. Each module is selected at compile time and contributes its
sources through conditional CMake, picking one implementation through Kconfig.
Being present in the repository does not enable anything: there is no plugin
manager and no central registry of sources.

## radio-uhf and Holybro SiK

```text
radio-uhf/
├── include/kfsw/modules/radio_uhf.h
├── parameters/radio_uhf_table.c
├── src/
│   ├── radio_uhf.c
│   └── radio_uhf_internal.h
└── holybro/src/holybro.c
```

The API reports the selected implementation, the hardware it expects, the
transparent-serial contract it expects, whether live status can be read, and
the RF link state. Most of that is a build-time fact rather than a readback:
Holybro has no safe live status query while the link is carrying traffic, so
status is unavailable and the link state stays `unknown`.

`CONFIG_KFSW_RADIO_UHF` enables the module and the implementation choice
selects `CONFIG_KFSW_RADIO_UHF_HOLYBRO`. The expected baud defaults to 57600.
The target devicetree still owns the UART and its pins, and `kfsw-comms` still
owns the data path.

Six values are published in **table 50**, all read-only:
`uhf_implementation`, `uhf_expected_hardware`, `uhf_expected_baud`,
`uhf_expected_flow`, `uhf_status_available` and `uhf_link_state`. There is no
writable TX power, network ID or air rate, because the module cannot apply one
— a parameter that accepts a write and does nothing is worse than no parameter.

SiK AT control is deferred for the same reason it is hard: entering command
mode interrupts the live serial path, and deciding who may do that and when is
a design in itself. The module never issues `AT&W`, `AT&F` or `ATS...`, and
never touches the verified `MAVLINK=1` setting.

The `uhf status` shell command reports the same snapshot. It does not repeat
`csp info`, interfaces, routes or UART counters.

## boton-test, a worked example

`boton_test` exists to show the whole boundary for a small stateful hardware
module at a size you can read in one sitting. Its operator-facing name is
`hw_test`, and it owns **table 67**.

```text
composition-chosen button -> edge ISR -> delayable work ---+
composition-chosen LEDs <-> owner LED setter <-> shell/PARAM +-> owner state
                                                            |        |
                                                            |        +-> typed status API
                                                            |                  |
                                                            +------------------+-> table 67
```

The source resolves `kfsw,boton-test-button` and names no board, MCU, GPIO
controller or pin. A target overlay maps that chosen phandle onto a real GPIO
node, and the devicetree flags decide active-low or active-high.

The ISR does one thing: reschedule a static delayable item on the system
workqueue. After the debounce interval the work reads the logical level. One
stable released-to-pressed transition counts, holding does not count again, and
a stable release rearms. Initialisation schedules that same sample after
interrupts are enabled, so a transition during GPIO setup is not lost.

It owns `press_count`, `last_press_s`, and independent green, blue and red LED
booleans. All start at zero on every boot; none persist; nothing is allocated
dynamically and no thread is created. `press_count` saturates at `UINT32_MAX`
rather than wrapping, and an accepted press still updates `last_press_s` after
that. Seconds come from platform monotonic milliseconds divided by 1000, and
also saturate rather than wrapping after about 136 years.

`kfsw_boton_test_get_status()` copies all five fields under one short mutex, so
a consumer that needs them to agree with each other gets a consistent snapshot
without touching GPIO or the parameter service. The parameter entries are
independent 32-bit views of the same state — fine for observing one value,
which is what they are for, but the typed API is what a multi-field consumer
should use.

The first physical mapping is the NUCLEO-L496ZG USER button with its three
LEDs: `led0`/LD1 green, `led1`/LD2 blue, `led2`/LD3 red. State, parameter,
saturation, shell and GPIO-emulator tests all run without that board; physical
evidence is a separate step with someone watching.

## Owning parameters

A module defines its own `struct kfsw_param_definition` entries, groups them in
a `struct kfsw_param_definition_set`, and the composition adds that set when
the module is enabled. Contributing one must never require editing
`parameter.c`, adding a dependency from the parameter service to a module, or
making a module aware of CSP.

Values are addressed by **table and offset**, and the table number says who
owns it. Modules use **50 to 99**; 1 to 24 are core and 25 to 49 are services.
Within its own table a module chooses offsets freely, so adding a value never
touches a registry anyone else shares — the only thing that has to stay unique
across the project is the table number.

| Table | Module | Values |
| --- | --- | --- |
| 50 | `radio-uhf` | 6, read-only |
| 67 | `boton-test`, as `hw_test` | 5: two counters, three LED controls |

Aggregation rejects a duplicate name or address with `-EEXIST`, so a collision
fails at startup rather than silently shadowing something.

## Module shape

A module may own its public interface, its parameters and what they mean, its
health reporting, its concrete implementations, its shell command and its
tests:

```text
radio-uhf/
  interface
  parameters
  health
  holybro/
    implementation
```

Create only the directories a real module needs. `radio-uhf` has an interface,
a table and a Holybro implementation, and no health directory, because it has
nothing to report yet. `boton-test` keeps its owner state and its GPIO adapter
apart so the semantics can be tested without a physical input.
