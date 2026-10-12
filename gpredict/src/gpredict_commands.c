#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include <zephyr/sys/util.h>

#include <kfsw/modules/gpredict.h>
#include <kfsw/services/command.h>

/*
 * The operator chooses the spacecraft and parks or clears the tracker through
 * the command registry, the same path the shell and the ground use.
 */

static void say(struct kfsw_command_result *result, int outcome, const char *reason)
{
	switch (outcome) {
	case -ENOENT:
		result->status = KFSW_COMMAND_INVALID_ARGUMENT;
		break;
	case -EPERM:
		result->status = KFSW_COMMAND_DENIED;
		break;
	case -EBUSY:
	case -EALREADY:
		result->status = KFSW_COMMAND_BUSY;
		break;
	default:
		result->status = KFSW_COMMAND_FAILED;
		(void)snprintf(result->detail, sizeof(result->detail), "%s (%d)", reason, outcome);
		return;
	}
	(void)snprintf(result->detail, sizeof(result->detail), "%s", reason);
}

static int profile_index(const struct kfsw_command_arg *arg, uint8_t *index,
			 struct kfsw_command_result *result)
{
	if (arg->value.u32 > UINT8_MAX) {
		say(result, -ENOENT, "no profile is defined there");
		return -EINVAL;
	}
	*index = (uint8_t)arg->value.u32;
	return 0;
}

static void say_state(struct kfsw_command_result *result)
{
	struct kfsw_gpredict_status status;

	(void)kfsw_gpredict_get_status(&status);
	result->status = KFSW_COMMAND_OK;
	(void)snprintf(result->detail, sizeof(result->detail), "state=%s",
		       kfsw_gpredict_state_name((enum kfsw_gpredict_state)status.state));
}

static int command_select(const struct kfsw_command_arg *args, size_t count,
			  const struct kfsw_command_source *source,
			  struct kfsw_command_result *result)
{
	struct kfsw_gpredict_profile profile;
	uint8_t index;
	int outcome;

	ARG_UNUSED(count);
	ARG_UNUSED(source);

	outcome = profile_index(&args[0], &index, result);
	if (outcome != 0) {
		return outcome;
	}
	outcome = kfsw_gpredict_profile_select(index);
	if (outcome == 0) {
		outcome = kfsw_gpredict_profile_get(index, &profile);
	}
	if (outcome != 0) {
		say(result, outcome,
		    (outcome == -EPERM) ? "the profile is disabled; enable it first"
					: "no profile is defined there");
		return outcome;
	}
	result->status = KFSW_COMMAND_OK;
	(void)snprintf(result->detail, sizeof(result->detail), "selected=%u name=%s catalogue=%u",
		       index, profile.name, profile.catalogue);
	return 0;
}

static int command_enable(const struct kfsw_command_arg *args, size_t count,
			  const struct kfsw_command_source *source,
			  struct kfsw_command_result *result)
{
	uint8_t index;
	int outcome;

	ARG_UNUSED(count);
	ARG_UNUSED(source);

	outcome = profile_index(&args[0], &index, result);
	if (outcome != 0) {
		return outcome;
	}
	if (args[1].value.u32 > 1U) {
		result->status = KFSW_COMMAND_INVALID_ARGUMENT;
		(void)snprintf(result->detail, sizeof(result->detail),
			       "write 1 to enable or 0 to disable");
		return -EINVAL;
	}
	outcome = kfsw_gpredict_profile_enable(index, args[1].value.u32 == 1U);
	if (outcome != 0) {
		say(result, outcome,
		    (outcome == -EBUSY) ? "the profile is selected; select another first"
					: "no profile is defined there");
		return outcome;
	}
	result->status = KFSW_COMMAND_OK;
	(void)snprintf(result->detail, sizeof(result->detail), "profile=%u enabled=%u", index,
		       args[1].value.u32);
	return 0;
}

static int command_park(const struct kfsw_command_arg *args, size_t count,
			const struct kfsw_command_source *source,
			struct kfsw_command_result *result)
{
	int outcome;

	ARG_UNUSED(args);
	ARG_UNUSED(count);
	ARG_UNUSED(source);

	outcome = kfsw_gpredict_park();
	if (outcome != 0) {
		say(result, outcome, "the tracker is in fault; clear it first");
		return outcome;
	}
	say_state(result);
	return 0;
}

static int command_clear(const struct kfsw_command_arg *args, size_t count,
			 const struct kfsw_command_source *source,
			 struct kfsw_command_result *result)
{
	int outcome;

	ARG_UNUSED(args);
	ARG_UNUSED(count);
	ARG_UNUSED(source);

	outcome = kfsw_gpredict_clear();
	if (outcome != 0) {
		say(result, outcome, "the tracker is not in fault");
		return outcome;
	}
	say_state(result);
	return 0;
}

static const enum kfsw_command_type select_args[] = {KFSW_COMMAND_TYPE_U32};
static const enum kfsw_command_type enable_args[] = {KFSW_COMMAND_TYPE_U32, KFSW_COMMAND_TYPE_U32};

static const struct kfsw_command_definition gpredict_commands[] = {
	{
		.id = KFSW_COMMAND_ID_GPREDICT_SELECT,
		.name = "gpredict_select",
		.help = "Judge the next bearing by a profile: gpredict_select <index>.",
		.flags = KFSW_COMMAND_FLAG_MUTATING,
		.arg_count = 1U,
		.arg_types = select_args,
		.handler = command_select,
	},
	{
		.id = KFSW_COMMAND_ID_GPREDICT_ENABLE,
		.name = "gpredict_enable",
		.help = "Allow or forbid selecting a profile: gpredict_enable <index> <1|0>.",
		.flags = KFSW_COMMAND_FLAG_MUTATING,
		.arg_count = 2U,
		.arg_types = enable_args,
		.handler = command_enable,
	},
	{
		.id = KFSW_COMMAND_ID_GPREDICT_PARK,
		.name = "gpredict_park",
		.help = "Send the antenna to its park position now.",
		.flags = KFSW_COMMAND_FLAG_MUTATING,
		.handler = command_park,
	},
	{
		.id = KFSW_COMMAND_ID_GPREDICT_CLEAR,
		.name = "gpredict_clear",
		.help = "Clear a tracker fault and park.",
		.flags = KFSW_COMMAND_FLAG_MUTATING,
		.handler = command_clear,
	},
};

const struct kfsw_command_definition_set kfsw_gpredict_command_definitions = {
	.commands = gpredict_commands,
	.count = ARRAY_SIZE(gpredict_commands),
};
