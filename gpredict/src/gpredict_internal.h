#ifndef KFSW_MODULES_GPREDICT_INTERNAL_H
#define KFSW_MODULES_GPREDICT_INTERNAL_H

#include <stdint.h>

#include <kfsw/modules/gpredict.h>

/** Shortest and longest grace period the owner accepts, in milliseconds. */
#define KFSW_GPREDICT_GRACE_MIN_MS 250U
#define KFSW_GPREDICT_GRACE_MAX_MS 600000U

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
#endif

#endif
