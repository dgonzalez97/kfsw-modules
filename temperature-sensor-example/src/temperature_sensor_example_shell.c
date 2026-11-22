#include <errno.h>
#include <stdint.h>

#include <zephyr/shell/shell.h>
#include <zephyr/sys/util.h>

#include <kfsw/modules/temperature_sensor_example.h>

static int cmd_temp_status(const struct shell *sh, size_t argc, char **argv)
{
	struct kfsw_temp_example_reading reading;
	int result;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	result = kfsw_temp_example_get(&reading);
	if (result != 0) {
		shell_error(sh, "temperature unavailable (%d)", result);
		return result;
	}

	if (reading.valid) {
		/* Whole degrees and thousandths, to avoid float printing. The sign is kept
		 * separately because -0.5 C has a whole part of 0.
		 */
		int32_t magnitude = (reading.milli_c < 0) ? -reading.milli_c : reading.milli_c;

		shell_print(sh, "die: %s%d.%03d C", (reading.milli_c < 0) ? "-" : "",
			    magnitude / 1000, magnitude % 1000);
	} else {
		shell_print(sh, "die: absent");
	}
	shell_print(sh, "samples: %u", reading.samples);
	shell_print(sh, "failures: %u", reading.failures);
	shell_print(sh, "alarm: %s, %u since boot", reading.alarm ? "on" : "off", reading.alarms);
	shell_print(sh, "last read at: %u ms", reading.last_uptime_ms);
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(temp_commands,
	SHELL_CMD_ARG(status, NULL, "Show the cached die temperature.", cmd_temp_status, 1, 0),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(temp, &temp_commands, "Temperature example: the MCU die reading.", NULL);
