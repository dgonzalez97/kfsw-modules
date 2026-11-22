# Temperature sensor example

The module to copy when a new device needs a K-FSW client. It reads one
sensor, keeps the latest value, and shows each way a module talks to the rest
of the system.

## Files

| File | Holds |
| --- | --- |
| `include/kfsw/modules/temperature_sensor_example.h` | The public API, the table, command and event IDs |
| `src/temperature_sensor_example.c` | The generic layer: cache, alarm, counters. No hardware |
| `src/temperature_sensor_example_sensor.c` | The hardware backend, built with `CONFIG_KFSW_TEMP_EXAMPLE_SENSOR` |
| `src/temperature_sensor_example_internal.h` | What the backend and the tests share with the generic layer |
| `parameters/temperature_sensor_example_table.c` | The PARAM table |
| `src/temperature_sensor_example_commands.c` | The command set, built with `CONFIG_KFSW_COMMAND` |
| `src/temperature_sensor_example_shell.c` | `temp status`, built with `CONFIG_KFSW_TEMP_EXAMPLE_SHELL` |
| `../tests/temperature_sensor_example/` | The Ztest suite, which builds without the backend |

`TODO(template)` marks the places a new module fills in for its own hardware.

## What it demonstrates

- **Generic layer and backend.** The backend has two functions,
  `kfsw_temp_example_sensor_prepare()` and `kfsw_temp_example_sensor_read()`.
  Everything else builds and runs on `native_sim` without it.
- **PARAM table 51.** Read-only telemetry (`temp_mcu_mc`, `temp_samples`,
  `temp_failures`, `temp_last_ms`, `temp_valid`, `temp_alarms`) refreshed by
  sample callbacks from `kfsw_temp_example_get()`, and one setting,
  `temp_alarm_mc`, which is writable and persistent. A persistent setting uses
  a `changed` callback, never `apply`: the snapshot restore refuses a
  persistent parameter with `apply`, and runs `changed` for each value it
  restores.
- **A command.** `temp_clear` (wire ID 64) zeroes the counters. The app
  registers `kfsw_temp_example_command_definitions` in `main.c`.
- **Events.** `KFSW_EVENT_TEMP_EXAMPLE_ALARM` (64) when a reading reaches the
  threshold and `KFSW_EVENT_TEMP_EXAMPLE_ALARM_CLEARED` (65) when it falls
  back, once per crossing. Modules have no event source of their own yet, so
  these use `KFSW_EVENT_SOURCE_APP` with IDs from 64.
- **Logging.** A failing sensor is logged when it starts failing and when it
  recovers, not every period.
- **Housekeeping.** Nothing in the module is HK-specific: a report selects
  parameters by table and offset. `hk define 0 51:0 51:20` collects the
  temperature and the alarm count; from the ground, the `hk_define` command
  takes the same text.

## Why a work queue of its own

A conversion can block on the sensor driver, and the system work queue also
runs the watchdog keep-alive. So the module polls on its own queue
(`CONFIG_KFSW_TEMP_EXAMPLE_STACK_SIZE`, `CONFIG_KFSW_TEMP_EXAMPLE_PRIORITY`).
A module whose reads never block uses a delayable work item on the system
queue instead.

## Tests and coverage

```bash
west twister -p native_sim/native/64 -T kfsw-modules/tests/temperature_sensor_example
./k-fsw/tools/ci/coverage.sh
```

The second runs every unit suite with gcovr and the filters in
`k-fsw/config/gcovr.cfg`; this module's lines are under
`kfsw-modules/temperature-sensor-example/` in the report.
