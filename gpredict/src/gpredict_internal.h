#ifndef KFSW_MODULES_GPREDICT_INTERNAL_H
#define KFSW_MODULES_GPREDICT_INTERNAL_H

#include <stdint.h>

#include <kfsw/modules/gpredict.h>

/** Shortest and longest grace period the owner accepts, in milliseconds. */
#define KFSW_GPREDICT_GRACE_MIN_MS 250U
#define KFSW_GPREDICT_GRACE_MAX_MS 600000U

/*
 * The travel the rotator is allowed, in millidegrees. Bearings outside it are
 * refused rather than clamped: a clamped bearing points at the wrong sky and
 * reports success while doing it.
 */
#define GPREDICT_AZIMUTH_MIN_MDEG (CONFIG_KFSW_GPREDICT_AZIMUTH_MIN_DEG * 1000)
#define GPREDICT_AZIMUTH_MAX_MDEG (CONFIG_KFSW_GPREDICT_AZIMUTH_MAX_DEG * 1000)
#define GPREDICT_ELEVATION_MIN_MDEG (CONFIG_KFSW_GPREDICT_ELEVATION_MIN_DEG * 1000)
#define GPREDICT_ELEVATION_MAX_MDEG (CONFIG_KFSW_GPREDICT_ELEVATION_MAX_DEG * 1000)

/** Count a bearing the predictor sent and the node refused, with its reason. */
void gpredict_refuse_bearing(const struct kfsw_gpredict_bearing *bearing, int error);

/**
 * Apply an event the public API cannot raise. The rotator reports that it
 * reached park or that it failed, and the hamlib piece passes those on.
 */
void gpredict_apply_event(enum kfsw_gpredict_event event);

/** Return to PARKED with the compiled grace period and every counter clear. */
void gpredict_state_reset(void);

/** Accept or refuse a proposed grace period. Returns 0 or -ERANGE. */
int gpredict_grace_validate(uint32_t grace_ms);

/** Install a grace period that gpredict_grace_validate() accepted. */
void gpredict_grace_set(uint32_t grace_ms);

/**
 * End the pass if the predictor has been quiet for a grace period. The work
 * handler is a call to this, so a test can drive it from its own clock.
 */
void gpredict_silence_check(void);

#if CONFIG_ZTEST
/** Park every lifetime counter at @p value, to reach saturation in a test. */
void gpredict_test_set_counters(uint32_t value);

/** Empty every profile slot and clear the selection. */
void gpredict_test_profiles_reset(void);
#endif

#endif
