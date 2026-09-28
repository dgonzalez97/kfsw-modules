# K-FSW Modules

Device and subsystem modules: the code for a specific piece of hardware, built
on the K-FSW services, comms and platform layers.

The examples cover a Holybro SiK UHF radio, board buttons and LEDs, and an
MCU temperature sensor. Add other device or subsystem modules here.

Full documentation is on the [K-FSW site](https://dgonzalez97.github.io/k-fsw/).

## Layers

A module handles one device or subsystem and uses the layers below it:

```text
  kfsw-modules   this repository: a radio, a sensor, a subsystem
       |  uses
       v
  kfsw-services  log, param, files, events, commands, health, update
  kfsw-comms     libcsp and its transports
  kfsw-platform  time, storage, reset cause, watchdog
```

For example, the Holybro radio sends bytes over a transparent serial link, and
the UART, KISS and CSP code stays in `kfsw-comms`.

Select modules at build time with Kconfig and bind their hardware in devicetree.

## Settings

A module defines its own parameter table, and the application registers it when
the module is enabled. The parameter service doesn't depend on any module.

Parameters are addressed by table and offset. Modules use tables 50 to 99;
1 to 24 are core and 25 to 49 are services. Inside its table a module can use
non-overlapping offsets. Table numbers and parameter names must be unique on
the node.

| Table | Module | Values |
| --- | --- | --- |
| 50 | `radio-uhf` | Identity, status, optional encryption settings |
| 51 | `temperature-sensor-example`, as `temp_example` | 5, read-only |
| 67 | `boton-test`, as `hw_test` | 5: two counters, three LED controls |

A duplicate name or address is rejected at startup.

## radio-uhf and Holybro SiK

The API reports the selected implementation, the expected hardware and serial
settings, whether live status can be read, and the RF link state. Most of this
is fixed at build time: the Holybro has no safe status query while it carries
traffic, so status is unavailable and the link state is `unknown`.

The target devicetree sets the UART and its pins, and `kfsw-comms` handles the
data. The expected baud rate defaults to 57600.

`CONFIG_KFSW_RADIO_UHF_CRYPTO` adds AES-256-GCM on flight and ground. Set
`uhf_key_hex` locally to a random 64-digit hex key; reading it back returns an
empty value. `uhf_encrypt_enable` is the master switch; `uhf_encrypt_tx` and
`uhf_encrypt_rx` select each direction. These settings accept local writes
only, and the radio module saves them outside the FTP directory. Check
`uhf_crypto_error`, then use `uhf connect` and `uhf status` to inspect sessions.

Packets keep using the current settings while a change is saved. A successful
save applies the change and clears the sessions; a failed save keeps the old
settings. Storage writes do not hold the packet-processing lock.

Authenticated handshakes and packet counters reject replayed data, including
after reset. A replayed handshake can interrupt a session. Receive decoding
runs outside the interrupt handler; transmit encoding runs in the caller.
The link keeps libcsp's KISS framing and leaves modem settings unchanged.

TX power, network ID and air rate are not writable because the module can't
apply them yet.

SiK AT commands are not supported yet. Entering command mode interrupts the
serial link, so it needs its own design. The module never sends `AT&W`, `AT&F`
or `ATS...` and doesn't change the `MAVLINK=1` setting.

## boton-test

`boton_test` is a small example of a hardware module with state. Its parameter
table is `hw_test`; the shell commands are `boton_test status` and `test led`.

```text
  button --> edge ISR --> debounce work ----+
  LEDs  <--> LED setter <--> shell          +--> module state
                                            |        |
                                            |        v
                                            +--> status API
```

The source names no board, MCU, GPIO controller or pin. A target overlay maps
the chosen properties to real GPIO nodes, and the devicetree flags set
active-low or active-high.

The ISR only reschedules the debounce work. After the debounce interval the
work reads the button: a stable change from released to pressed counts once,
holding doesn't count again, and a stable release re-arms it. Init runs the same
check after enabling interrupts, so a press during GPIO setup is not lost.

The module keeps a press count, the time of the last press and three LED
states. They start at zero on every boot and are not saved. Nothing is
allocated, no thread is created, and the counters saturate instead of wrapping.

The status API copies all five values under one lock. The parameters are read
one at a time, which is fine for watching a single value.

The first hardware mapping is the NUCLEO USER button and its three LEDs. The
state, settings, shell and GPIO emulator tests run without the board; the
hardware test is in `k-fsw/tests/hil/boton-test/`.

## temperature-sensor-example

An example of a sensor read into a parameter table, and the first use of the
Zephyr sensor API in the project. On a NUCLEO-L496ZG it reads the
factory-calibrated die temperature on ADC1 channel 17, so it needs no wiring
and gives housekeeping a real value to collect.

Reading the ADC takes a driver mutex and sample callbacks run under the table
lock, so the module polls on its own work queue and the table copies the cached
reading. If a read fails, `temp_mcu_mc` is set to a reserved value far outside
any real temperature, `temp_valid` goes to 0 and `temp_failures` increases.

A cached sample older than `KFSW_TEMP_EXAMPLE_MAX_AGE_MS` also reads as invalid.
The failure counter counts failed reads, not age checks.

It measures the die, not the board or the air, and is only accurate to a few
degrees, so use it as a trend.

## Module layout

A module can have a public interface, a parameter table, health reporting,
implementations, a shell command and tests. Only create the directories you
need: `radio-uhf` has an interface, a table and a Holybro implementation, but no
health directory.

## License

Licensed under [Apache 2.0](LICENSE). Third-party dependencies retain their
own licences.
