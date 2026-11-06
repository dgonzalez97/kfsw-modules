# button_test

A debounced button and three LEDs: the smallest module that reads an input
and drives an output. Start from `temperature-sensor-example` for a new
module; this one shows the two things that example does not have, an
interrupt-driven input and a setting whose write can fail.

## Files

| File | Holds |
| --- | --- |
| `include/kfsw/modules/button_test.h` | The public API and the table ID |
| `src/button_test.c` | The generic layer: debounced state, press count, LED state. No hardware |
| `src/button_test_gpio.c` | The GPIO backend, built with `CONFIG_KFSW_BUTTON_TEST_GPIO` or `CONFIG_KFSW_BUTTON_TEST_LED_GPIO` |
| `parameters/button_test_table.c` | PARAM table 67 |
| `src/button_test_shell.c` | `button_test status` and `test led`, built with `CONFIG_KFSW_BUTTON_TEST_SHELL` |
| `../tests/button_test/` | The Ztest suite, with and without emulated GPIO |

`TODO(template)` marks where a new module fills in its own hardware.

## Patterns

- **Interrupt to work item.** The GPIO edge callback only reschedules a
  delayable work item (`button_test_gpio.c`); the debounced level is read and
  counted there, under the module's mutex, never in the interrupt.
- **Telemetry from a snapshot.** `press_count` and `last_press_s` are
  read-only and refreshed by sample callbacks from
  `kfsw_button_test_get_status()`. The table keeps its own copy, so the
  parameter service never reads state guarded by the module's lock.
- **A setting that can fail.** `led_green`, `led_blue` and `led_red` use
  `apply`, which drives the GPIO before the value is stored: a GPIO error is
  returned to the writer and the stored value does not change. A setting
  that must survive a reset uses `changed` instead (see the temperature
  example).

The parameter names and the `hw_test` table name predate the rule that names
start with the module's name. They stay because the ground and
`k-fsw/tests/button-test-smoke.sh` use them.

## Tests and coverage

```bash
west twister -p native_sim/native/64 -T kfsw-modules/tests/button_test
./k-fsw/tools/ci/coverage.sh
```
