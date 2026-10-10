#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#include <kfsw/services/log.h>

#include "gpredict_internal.h"

/*
 * A slot holds a profile when its name is not empty. Define refuses an empty
 * name, so no second flag is needed to say the slot is in use.
 *
 * The lock covers the slots and the selection. A bearing is judged with it
 * held, so a select lands strictly before or after that bearing. Judging takes
 * the tracker lock inside this one, and the tracker never calls in here, so the
 * order is always profile lock then tracker lock and cannot invert.
 */
static struct kfsw_gpredict_profile profiles[CONFIG_KFSW_GPREDICT_PROFILES];
static int selected = -ENOENT;
static K_MUTEX_DEFINE(profile_lock);

static bool slot_defined(uint8_t index)
{
	return (index < ARRAY_SIZE(profiles)) && (profiles[index].name[0] != '\0');
}

/* A profile narrows the rotator's travel and never widens it. */
static bool travel_fits(const struct kfsw_gpredict_profile *profile)
{
	return (profile->azimuth_min_mdeg <= profile->azimuth_max_mdeg) &&
	       (profile->elevation_min_mdeg <= profile->elevation_max_mdeg) &&
	       (profile->azimuth_min_mdeg >= GPREDICT_AZIMUTH_MIN_MDEG) &&
	       (profile->azimuth_max_mdeg <= GPREDICT_AZIMUTH_MAX_MDEG) &&
	       (profile->elevation_min_mdeg >= GPREDICT_ELEVATION_MIN_MDEG) &&
	       (profile->elevation_max_mdeg <= GPREDICT_ELEVATION_MAX_MDEG);
}

static bool profile_valid(const struct kfsw_gpredict_profile *profile)
{
	/* No terminator within the name is too long; one at the first byte is empty. */
	const char *terminator = memchr(profile->name, '\0', sizeof(profile->name));

	if ((terminator == NULL) || (terminator == profile->name)) {
		return false;
	}
	if (!travel_fits(profile)) {
		return false;
	}
	if (profile->policy > KFSW_GPREDICT_FREQUENCY_IGNORE) {
		return false;
	}
	/* HOLD with nothing to hold would tune the radio to zero every cycle. */
	return (profile->policy != KFSW_GPREDICT_FREQUENCY_HOLD) || (profile->frequency_hz != 0U);
}

static bool within_profile(const struct kfsw_gpredict_profile *profile,
			   const struct kfsw_gpredict_bearing *bearing)
{
	return (bearing->azimuth_mdeg >= profile->azimuth_min_mdeg) &&
	       (bearing->azimuth_mdeg <= profile->azimuth_max_mdeg) &&
	       (bearing->elevation_mdeg >= profile->elevation_min_mdeg) &&
	       (bearing->elevation_mdeg <= profile->elevation_max_mdeg);
}

int kfsw_gpredict_profile_define(uint8_t index, const struct kfsw_gpredict_profile *profile)
{
	bool deselected = false;

	if (profile == NULL) {
		return -EINVAL;
	}
	if (index >= ARRAY_SIZE(profiles)) {
		return -ENOSPC;
	}
	if (!profile_valid(profile)) {
		kfsw_log_warning("Pass tracking refused profile %u", index);
		return -EINVAL;
	}

	(void)k_mutex_lock(&profile_lock, K_FOREVER);
	profiles[index] = *profile;
	if ((selected == (int)index) && !profile->enabled) {
		/* A disabled selection would judge against rules nobody can select. */
		selected = -ENOENT;
		deselected = true;
	}
	k_mutex_unlock(&profile_lock);

	kfsw_log_info("Pass tracking profile %u is %s, catalogue %u", index, profile->name,
		      profile->catalogue);
	if (deselected) {
		kfsw_log_warning("Pass tracking profile %u disabled; none selected", index);
	}
	return 0;
}

int kfsw_gpredict_profile_select(uint8_t index)
{
	char name[KFSW_GPREDICT_PROFILE_NAME_MAX] = "";
	int result = 0;

	(void)k_mutex_lock(&profile_lock, K_FOREVER);
	if (!slot_defined(index)) {
		result = -ENOENT;
	} else if (!profiles[index].enabled) {
		result = -EPERM;
	} else {
		selected = (int)index;
		(void)memcpy(name, profiles[index].name, sizeof(name));
	}
	k_mutex_unlock(&profile_lock);

	if (result != 0) {
		kfsw_log_warning("Pass tracking refused to select profile %u: %d", index, result);
	} else {
		kfsw_log_info("Pass tracking selected profile %u, %s", index, name);
	}
	return result;
}

int kfsw_gpredict_profile_enable(uint8_t index, bool enabled)
{
	int result = 0;

	(void)k_mutex_lock(&profile_lock, K_FOREVER);
	if (!slot_defined(index)) {
		result = -ENOENT;
	} else if (!enabled && (selected == (int)index)) {
		result = -EBUSY;
	} else {
		profiles[index].enabled = enabled;
	}
	k_mutex_unlock(&profile_lock);

	if (result != 0) {
		kfsw_log_warning("Pass tracking refused to set profile %u enabled=%u: %d", index,
				 enabled ? 1U : 0U, result);
	} else {
		kfsw_log_info("Pass tracking profile %u enabled=%u", index, enabled ? 1U : 0U);
	}
	return result;
}

int kfsw_gpredict_profile_get(uint8_t index, struct kfsw_gpredict_profile *profile)
{
	int result = 0;

	if (profile == NULL) {
		return -EINVAL;
	}

	(void)k_mutex_lock(&profile_lock, K_FOREVER);
	if (!slot_defined(index)) {
		result = -ENOENT;
	} else {
		*profile = profiles[index];
	}
	k_mutex_unlock(&profile_lock);
	return result;
}

int kfsw_gpredict_profile_selected(void)
{
	int index;

	(void)k_mutex_lock(&profile_lock, K_FOREVER);
	index = selected;
	k_mutex_unlock(&profile_lock);
	return index;
}

int kfsw_gpredict_profile_bearing(const struct kfsw_gpredict_bearing *bearing)
{
	int result;

	if (bearing == NULL) {
		return -EINVAL;
	}

	(void)k_mutex_lock(&profile_lock, K_FOREVER);
	if (selected < 0) {
		result = -ENOENT;
	} else if (!within_profile(&profiles[selected], bearing)) {
		/* Counted with the tracker's own refusals, so a profile that refuses
		 * a whole pass shows on gp_refusals rather than as a quiet antenna.
		 */
		gpredict_refuse_bearing(bearing, -EINVAL);
		result = -EINVAL;
	} else {
		result = kfsw_gpredict_bearing(bearing);
	}
	k_mutex_unlock(&profile_lock);
	return result;
}

int kfsw_gpredict_profile_frequency(uint64_t offered, uint64_t *send)
{
	int result = 0;

	if (send == NULL) {
		return -EINVAL;
	}
	/* Anything but a decision forwards nothing, whatever the caller checks. */
	*send = 0U;
	if (offered == 0U) {
		return -EINVAL;
	}

	(void)k_mutex_lock(&profile_lock, K_FOREVER);
	if (selected < 0) {
		result = -ENOENT;
	} else if (profiles[selected].policy == KFSW_GPREDICT_FREQUENCY_FOLLOW) {
		*send = offered;
	} else if (profiles[selected].policy == KFSW_GPREDICT_FREQUENCY_HOLD) {
		*send = profiles[selected].frequency_hz;
	}
	k_mutex_unlock(&profile_lock);
	return result;
}

#if CONFIG_ZTEST
void gpredict_test_profiles_reset(void)
{
	(void)k_mutex_lock(&profile_lock, K_FOREVER);
	(void)memset(profiles, 0, sizeof(profiles));
	selected = -ENOENT;
	k_mutex_unlock(&profile_lock);
}
#endif
