#include <errno.h>
#include <stdio.h>

#include <zephyr/sys/util.h>

#include <kfsw/modules/temperature_sensor_example.h>
#include <kfsw/services/command.h>

static int command_temp_clear(const struct kfsw_command_arg *args, size_t count,
			      const struct kfsw_command_source *source,
			      struct kfsw_command_result *result)
{
	struct kfsw_temp_example_reading reading;
	int outcome;

	ARG_UNUSED(args);
	ARG_UNUSED(count);
	ARG_UNUSED(source);

	outcome = kfsw_temp_example_clear();
	if (outcome != 0) {
		result->status = KFSW_COMMAND_UNAVAILABLE;
		(void)snprintf(result->detail, sizeof(result->detail), "not initialized");
		return outcome;
	}
	(void)kfsw_temp_example_get(&reading);
	result->status = KFSW_COMMAND_OK;
	(void)snprintf(result->detail, sizeof(result->detail), "samples=%u failures=%u alarms=%u",
		       reading.samples, reading.failures, reading.alarms);
	return 0;
}

static const struct kfsw_command_definition temp_example_commands[] = {
	{
		.id = KFSW_COMMAND_ID_TEMP_EXAMPLE_CLEAR,
		.name = "temp_clear",
		.help = "Zero the temperature read, failure and alarm counters.",
		.flags = KFSW_COMMAND_FLAG_MUTATING,
		.handler = command_temp_clear,
	},
};

const struct kfsw_command_definition_set kfsw_temp_example_command_definitions = {
	.commands = temp_example_commands,
	.count = ARRAY_SIZE(temp_example_commands),
};
