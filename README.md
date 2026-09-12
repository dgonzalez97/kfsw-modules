# K-FSW Modules

Device and subsystem modules — the code that knows about a particular piece of
hardware, as opposed to the generic mechanisms it runs on.

Two exist today: **`radio-uhf`**, a UHF equipment module with a Holybro SiK
implementation, and **`boton-test`**, a deliberately small worked example of
what owning hardware looks like. GNSS receivers, ADCS or EPS devices and
payloads would belong here too; none are written.

Full documentation is on the
[K-FSW site](https://dgonzalez97.github.io/k-fsw/).

## What belongs here

A module owns hardware. It does not own the mechanisms it uses to reach that
hardware:

```text
  kfsw-modules   ← this repository: a radio, a sensor, a subsystem
       │  uses, never owns
       ▼
  kfsw-services  log, param, files, events, commands, health, update
  kfsw-comms     libcsp and the transports under it
  kfsw-platform  time, storage, reset cause, watchdog
```

Using a mechanism is not taking it over. The Holybro radio moves bytes over a
transparent serial link, and doing so leaves UART, KISS, CSP and packet
ownership exactly where they were.

Each module is selected at compile time. Being present in the repository does
not enable anything: there is no plugin manager and no central registry.

## Owning settings

A module defines its own settings, groups them, and the composition adds that
group when the module is enabled. Contributing one must never require editing
the parameter service, adding a dependency from that service to a module, or
making a module aware of CSP.

Settings are addressed by **table and offset**, and the table number says who
owns it. Modules use **50 to 99**; 1 to 24 are core and 25 to 49 are services.
Within its own table a module picks offsets freely, so adding a value never
touches anything anyone else shares — the only thing that must stay unique
across the project is the table number.

| Table | Module | Values |
| --- | --- | --- |
| 50 | `radio-uhf` | 6, read-only |
| 51 | `temperature-sensor-example`, as `temp_example` | 5, read-only |
| 67 | `boton-test`, as `hw_test` | 5: two counters, three LED controls |

A duplicate name or address is refused at startup rather than silently
shadowing something.

## radio-uhf and Holybro SiK

The API reports the selected implementation, the hardware it expects, the
serial contract it expects, whether live status can be read, and the RF link
state. Most of that is a build-time fact rather than a readback: Holybro has no
safe status query while the link is carrying traffic, so status is unavailable
and the link state stays `unknown`.

The target devicetree owns the UART and its pins, and `kfsw-comms` owns the
data path. The expected baud defaults to 57600.

There is no writable TX power, network ID or air rate, because the module
cannot apply one — a setting that accepts a write and does nothing is worse
than no setting at all.

SiK AT control is deferred for the reason that makes it hard: entering command
mode interrupts the live serial path, and deciding who may do that and when is
a design in itself. The module never issues `AT&W`, `AT&F` or `ATS...`, and
never touches the verified `MAVLINK=1` setting.

## boton-test, a worked example

`boton_test` exists to show the whole boundary for a small stateful hardware
module at a size you can read in one sitting. Its operator-facing name is
`hw_test`.

```text
  chosen button ──► edge ISR ──► debounced work ──┐
  chosen LEDs  ◄──► owner LED setter ◄──► shell   ├──► owner state
                                                  │        │
                                                  │        ▼
                                                  └──► typed status API
```

The source names no board, MCU, GPIO controller or pin — a target overlay maps
a chosen phandle onto a real GPIO node, and the devicetree flags decide
active-low or active-high.

The ISR does one thing: reschedule a debounce. After the interval the work
reads the logical level. One stable released-to-pressed transition counts,
holding does not count again, and a stable release rearms. Initialisation
schedules the same sample after interrupts are enabled, so a transition during
GPIO setup is not lost.

It owns a press count, the time of the last press, and three LED booleans. All
start at zero on every boot; none persist; nothing is allocated dynamically and
no thread is created. Counters saturate rather than wrapping.

Reading all five fields at once goes through the typed status API, which copies
them under one short lock — the individual settings are independent views, fine
for watching one value, which is what they are for.

The first physical mapping is the STM32 Nucleo USER button with its three LEDs.
State, settings, saturation, shell and GPIO-emulator tests all run without that
board; physical evidence is a separate step with someone watching.

## temperature-sensor-example

The worked example of a sensor behind a parameter table, and the first thing in
the project to call the Zephyr sensor API at all. On a NUCLEO-L496ZG it reads
the factory-calibrated die temperature on ADC1 channel 17, so it needs no
wiring and gives housekeeping something physical to collect — a value a person
can change by putting a finger on the chip, which no counter can do.

Two things it is careful about. Reading the ADC takes a driver mutex and
parameter sample callbacks run under the table lock, so the module polls on its
own schedule and the table only ever copies a cached reading. And a read that
fails drops the last good number rather than keeping it: `temp_mcu_mc` becomes
a reserved value far outside anything a die survives, `temp_valid` goes to
zero, and `temp_failures` counts. A stale temperature served forever reads
exactly like a working sensor.

It reports the die, not the board and not the air around it, and the absolute
accuracy is a few degrees. Read it as a trend.

## Module shape

A module may own its public interface, its settings and what they mean, its
health reporting, its concrete implementations, its shell command and its
tests. Create only the directories a real module needs — `radio-uhf` has an
interface, a table and a Holybro implementation, and no health directory,
because it has nothing to report yet.

## License

Licensed under [Apache 2.0](LICENSE). Third-party dependencies retain their
own licences.
