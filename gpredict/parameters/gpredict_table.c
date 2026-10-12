#include <stdint.h>
#include <string.h>

#include <zephyr/sys/util.h>

#include <kfsw/modules/gpredict.h>
#include <kfsw/services/parameter.h>

#include "gpredict_internal.h"

/*
 * What an operator can see and change during a pass. Everything except the
 * grace period is owned by the tracker, so everything except the grace period
 * is read-only: a writable state would let the ground claim a pass that the
 * rotator is not flying.
 */

static uint8_t gp_state;
static int32_t gp_azimuth_mdeg;
static int32_t gp_elevation_mdeg;
static int32_t gp_actual_azimuth_mdeg;
static int32_t gp_actual_elevation_mdeg;
static uint64_t gp_frequency_hz;
static uint32_t gp_silence_ms;
static uint32_t gp_grace_ms = CONFIG_KFSW_GPREDICT_GRACE_MS;
static uint32_t gp_bearings;
static uint32_t gp_refusals;
static uint32_t gp_passes_ended;
static uint32_t gp_faults;
static int32_t gp_last_error;

/* Reserved for no selection: 0 is a real slot and a real policy. */
#define GP_NONE UINT8_MAX

static uint8_t gp_profile = GP_NONE;
static uint8_t gp_policy = GP_NONE;
static uint8_t gp_profiles;
static uint8_t gp_profiles_on;
static uint32_t gp_catalogue;
static char gp_profile_name[KFSW_GPREDICT_PROFILE_NAME_MAX];

static void sample_tracker(void)
{
	struct kfsw_gpredict_status status;

	if (kfsw_gpredict_get_status(&status) != 0) {
		return;
	}
	gp_state = status.state;
	gp_azimuth_mdeg = status.azimuth_mdeg;
	gp_elevation_mdeg = status.elevation_mdeg;
	gp_actual_azimuth_mdeg = status.actual_azimuth_mdeg;
	gp_actual_elevation_mdeg = status.actual_elevation_mdeg;
	gp_frequency_hz = status.frequency_hz;
	gp_silence_ms = status.silence_ms;
	gp_grace_ms = status.grace_ms;
	gp_bearings = status.bearings;
	gp_refusals = status.refusals;
	gp_passes_ended = status.passes_ended;
	gp_faults = status.faults;
	gp_last_error = (int32_t)status.last_error;
}

#define GPREDICT_SAMPLE(field, type)                                                               \
	static void sample_##field(void *value)                                                    \
	{                                                                                          \
		sample_tracker();                                                                  \
		*(type *)value = gp_##field;                                                       \
	}

GPREDICT_SAMPLE(state, uint8_t)
GPREDICT_SAMPLE(azimuth_mdeg, int32_t)
GPREDICT_SAMPLE(elevation_mdeg, int32_t)
GPREDICT_SAMPLE(actual_azimuth_mdeg, int32_t)
GPREDICT_SAMPLE(actual_elevation_mdeg, int32_t)
GPREDICT_SAMPLE(frequency_hz, uint64_t)
GPREDICT_SAMPLE(silence_ms, uint32_t)
GPREDICT_SAMPLE(grace_ms, uint32_t)
GPREDICT_SAMPLE(bearings, uint32_t)
GPREDICT_SAMPLE(refusals, uint32_t)
GPREDICT_SAMPLE(passes_ended, uint32_t)
GPREDICT_SAMPLE(faults, uint32_t)
GPREDICT_SAMPLE(last_error, int32_t)

static void sample_slots(void)
{
	const int selected = kfsw_gpredict_profile_selected();
	struct kfsw_gpredict_profile profile;

	gp_profile = GP_NONE;
	gp_policy = GP_NONE;
	gp_profiles = 0U;
	gp_profiles_on = 0U;
	gp_catalogue = 0U;
	gp_profile_name[0] = '\0';

	for (uint8_t index = 0U; index < CONFIG_KFSW_GPREDICT_PROFILES; index++) {
		if (kfsw_gpredict_profile_get(index, &profile) != 0) {
			continue;
		}
		gp_profiles++;
		if (profile.enabled) {
			gp_profiles_on++;
		}
		if ((int)index == selected) {
			gp_profile = index;
			gp_policy = profile.policy;
			gp_catalogue = profile.catalogue;
			(void)memcpy(gp_profile_name, profile.name, sizeof(gp_profile_name));
		}
	}
}

#define PROFILE_SAMPLE(field, type)                                                                \
	static void sample_##field(void *value)                                                    \
	{                                                                                          \
		sample_slots();                                                                    \
		*(type *)value = gp_##field;                                                       \
	}

PROFILE_SAMPLE(profile, uint8_t)
PROFILE_SAMPLE(policy, uint8_t)
PROFILE_SAMPLE(profiles, uint8_t)
PROFILE_SAMPLE(profiles_on, uint8_t)
PROFILE_SAMPLE(catalogue, uint32_t)

static void sample_profile_name(void *value)
{
	/* value is gp_profile_name, which sample_slots() has just filled. */
	ARG_UNUSED(value);
	sample_slots();
}

static int validate_grace(const union kfsw_param_scalar *value)
{
	return gpredict_grace_validate(value->u32);
}

static void grace_changed(const union kfsw_param_scalar *value)
{
	gpredict_grace_set(value->u32);
}

static const struct kfsw_param_definition gpredict_param_definitions[] = {
	{
		.offset = 0x00U,
		.type = KFSW_PARAM_U8,
		.flags = KFSW_PARAM_FLAG_READ_ONLY | KFSW_PARAM_FLAG_LIVE,
		.name = "gp_state",
		.description = "parked, tracking, holding, parking or fault",
		.value = &gp_state,
		.sample = sample_state,
	},
	{
		.offset = 0x04U,
		.type = KFSW_PARAM_I32,
		.flags = KFSW_PARAM_FLAG_READ_ONLY | KFSW_PARAM_FLAG_LIVE,
		.name = "gp_az_mdeg",
		.unit = "mdeg",
		.description = "Azimuth last accepted from the predictor",
		.value = &gp_azimuth_mdeg,
		.sample = sample_azimuth_mdeg,
	},
	{
		.offset = 0x08U,
		.type = KFSW_PARAM_I32,
		.flags = KFSW_PARAM_FLAG_READ_ONLY | KFSW_PARAM_FLAG_LIVE,
		.name = "gp_el_mdeg",
		.unit = "mdeg",
		.description = "Elevation last accepted from the predictor",
		.value = &gp_elevation_mdeg,
		.sample = sample_elevation_mdeg,
	},
	{
		.offset = 0x0cU,
		.type = KFSW_PARAM_I32,
		.flags = KFSW_PARAM_FLAG_READ_ONLY | KFSW_PARAM_FLAG_LIVE,
		.name = "gp_actual_az_mdeg",
		.unit = "mdeg",
		.description = "Azimuth the rotator last reported, not the commanded one",
		.value = &gp_actual_azimuth_mdeg,
		.sample = sample_actual_azimuth_mdeg,
	},
	{
		.offset = 0x10U,
		.type = KFSW_PARAM_I32,
		.flags = KFSW_PARAM_FLAG_READ_ONLY | KFSW_PARAM_FLAG_LIVE,
		.name = "gp_actual_el_mdeg",
		.unit = "mdeg",
		.description = "Elevation the rotator last reported, not the commanded one",
		.value = &gp_actual_elevation_mdeg,
		.sample = sample_actual_elevation_mdeg,
	},
	{
		.offset = 0x18U,
		.type = KFSW_PARAM_U64,
		.flags = KFSW_PARAM_FLAG_READ_ONLY | KFSW_PARAM_FLAG_LIVE,
		.name = "gp_frequency_hz",
		.unit = "Hz",
		.description = "Doppler-corrected frequency last accepted; 0 if none",
		.value = &gp_frequency_hz,
		.sample = sample_frequency_hz,
	},
	{
		.offset = 0x20U,
		.type = KFSW_PARAM_U32,
		.flags = KFSW_PARAM_FLAG_READ_ONLY | KFSW_PARAM_FLAG_LIVE,
		.name = "gp_silence_ms",
		.unit = "ms",
		.description = "Since the last bearing; 0 while none has arrived",
		.value = &gp_silence_ms,
		.sample = sample_silence_ms,
	},
	{
		.offset = 0x24U,
		.type = KFSW_PARAM_U32,
		.flags = KFSW_PARAM_FLAG_CONFIGURATION,
		.name = "gp_grace_ms",
		.unit = "ms",
		.description = "Silence the antenna holds through before it parks",
		.value = &gp_grace_ms,
		.default_value.u32 = CONFIG_KFSW_GPREDICT_GRACE_MS,
		.validate = validate_grace,
		.changed = grace_changed,
		.sample = sample_grace_ms,
	},
	{
		.offset = 0x28U,
		.type = KFSW_PARAM_U32,
		.flags = KFSW_PARAM_FLAG_READ_ONLY,
		.name = "gp_bearings",
		.description = "Bearings accepted since boot; saturates",
		.value = &gp_bearings,
		.sample = sample_bearings,
	},
	{
		.offset = 0x2cU,
		.type = KFSW_PARAM_U32,
		.flags = KFSW_PARAM_FLAG_READ_ONLY,
		.name = "gp_refusals",
		.description = "Bearings refused outside the travel or in fault; saturates",
		.value = &gp_refusals,
		.sample = sample_refusals,
	},
	{
		.offset = 0x30U,
		.type = KFSW_PARAM_U32,
		.flags = KFSW_PARAM_FLAG_READ_ONLY,
		.name = "gp_passes_ended",
		.description = "Passes that ended because the predictor went quiet",
		.value = &gp_passes_ended,
		.sample = sample_passes_ended,
	},
	{
		.offset = 0x34U,
		.type = KFSW_PARAM_U32,
		.flags = KFSW_PARAM_FLAG_READ_ONLY,
		.name = "gp_faults",
		.description = "Entries into fault since boot; saturates",
		.value = &gp_faults,
		.sample = sample_faults,
	},
	{
		.offset = 0x38U,
		.type = KFSW_PARAM_I32,
		.flags = KFSW_PARAM_FLAG_READ_ONLY,
		.name = "gp_last_error",
		.description = "Why the last refusal or fault happened, or 0",
		.value = &gp_last_error,
		.sample = sample_last_error,
	},
	{
		.offset = 0x3cU,
		.type = KFSW_PARAM_U8,
		.flags = KFSW_PARAM_FLAG_READ_ONLY | KFSW_PARAM_FLAG_LIVE,
		.name = "gp_profile",
		.description = "Selected profile slot; 255 when none is selected",
		.value = &gp_profile,
		.sample = sample_profile,
	},
	{
		.offset = 0x3dU,
		.type = KFSW_PARAM_U8,
		.flags = KFSW_PARAM_FLAG_READ_ONLY | KFSW_PARAM_FLAG_LIVE,
		.name = "gp_policy",
		.description = "Frequency policy: 0 follow, 1 hold, 2 ignore; 255 when none",
		.value = &gp_policy,
		.sample = sample_policy,
	},
	{
		.offset = 0x3eU,
		.type = KFSW_PARAM_U8,
		.flags = KFSW_PARAM_FLAG_READ_ONLY,
		.name = "gp_profiles",
		.description = "Profile slots holding a profile",
		.value = &gp_profiles,
		.sample = sample_profiles,
	},
	{
		.offset = 0x3fU,
		.type = KFSW_PARAM_U8,
		.flags = KFSW_PARAM_FLAG_READ_ONLY,
		.name = "gp_profiles_on",
		.description = "Defined profiles that may be selected",
		.value = &gp_profiles_on,
		.sample = sample_profiles_on,
	},
	{
		.offset = 0x40U,
		.type = KFSW_PARAM_U32,
		.flags = KFSW_PARAM_FLAG_READ_ONLY | KFSW_PARAM_FLAG_LIVE,
		.name = "gp_catalogue",
		.description = "Catalogue number of the selected profile; 0 when none",
		.value = &gp_catalogue,
		.sample = sample_catalogue,
	},
	{
		.offset = 0x44U,
		.type = KFSW_PARAM_STRING,
		.capacity = KFSW_GPREDICT_PROFILE_NAME_MAX,
		.flags = KFSW_PARAM_FLAG_READ_ONLY | KFSW_PARAM_FLAG_LIVE,
		.name = "gp_profile_name",
		.description = "Name of the selected profile; empty when none",
		.value = gp_profile_name,
		.sample = sample_profile_name,
	},
};

const struct kfsw_param_definition_set kfsw_gpredict_param_definitions = {
	.table = KFSW_GPREDICT_TABLE_ID,
	.name = KFSW_GPREDICT_TABLE_NAME,
	.description = "Pass tracking state, bearings and grace period",
	.definitions = gpredict_param_definitions,
	.count = ARRAY_SIZE(gpredict_param_definitions),
};
