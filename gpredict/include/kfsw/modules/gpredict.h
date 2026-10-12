#ifndef KFSW_MODULES_GPREDICT_H
#define KFSW_MODULES_GPREDICT_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct kfsw_param_definition_set;
struct kfsw_command_definition_set;

/** Parameter table reserved for pass tracking. */
#define KFSW_GPREDICT_TABLE_ID 52U
/** Parameter table name. */
#define KFSW_GPREDICT_TABLE_NAME "gpredict"

/**
 * @defgroup kfsw_modules_gpredict Pass tracking
 * @ingroup kfsw_modules
 *
 * Following a spacecraft across a pass, driven by a predictor on the ground.
 *
 * Gpredict computes the pass and speaks the hamlib protocols: it sends a
 * bearing to a rotator daemon and a corrected frequency to a radio daemon.
 * This module is the other end of those two conversations, so prediction stays
 * where it already works and this node only has to point and tune.
 *
 * Every question worth getting right here is about what happens when the
 * conversation stops. A rotator left at the last bearing it was given points
 * at nothing; one that returns to its park position in the middle of a pass is
 * worse, because the pass is lost and nobody is told. So the decision is a
 * state machine with a grace period: a gap shorter than the grace is a gap in
 * the predictor, and the antenna holds; a gap longer than it is the end of the
 * pass, and the antenna parks.
 *
 * @{
 */

/** Where the tracker is. */
enum kfsw_gpredict_state {
	/** Nothing has ever been tracked, or the last pass finished and parked. */
	KFSW_GPREDICT_PARKED = 0,
	/** Bearings are arriving and the antenna is following them. */
	KFSW_GPREDICT_TRACKING = 1,
	/**
	 * Bearings stopped arriving but not for long enough to call the pass
	 * over. The antenna holds its last bearing, because a predictor that
	 * stalls for a second is not a pass that ended.
	 */
	KFSW_GPREDICT_HOLDING = 2,
	/**
	 * On the way to the park position, after the pass or on command. It is
	 * left when the rotator reports it arrived, so until a rotator driver
	 * exists this state is where a build without one stays.
	 */
	KFSW_GPREDICT_PARKING = 3,
	/**
	 * The rotator reported a problem, or a bearing was refused. Tracking
	 * does not resume on its own: an operator has to clear it, because a
	 * fault that clears itself hides a rotator that is failing.
	 */
	KFSW_GPREDICT_FAULT = 4,
};

/** What happened. The state machine is driven only by these. */
enum kfsw_gpredict_event {
	/** A bearing arrived from the predictor. */
	KFSW_GPREDICT_EVENT_BEARING = 0,
	/** The grace period expired with no bearing. */
	KFSW_GPREDICT_EVENT_SILENCE = 1,
	/** The rotator reported it reached the park position. */
	KFSW_GPREDICT_EVENT_PARKED = 2,
	/** An operator asked for the park position now. */
	KFSW_GPREDICT_EVENT_PARK_COMMANDED = 3,
	/** The rotator or the link to it failed. */
	KFSW_GPREDICT_EVENT_FAULT = 4,
	/** An operator cleared a fault. */
	KFSW_GPREDICT_EVENT_CLEARED = 5,
};

/** A bearing, as the predictor gives it. */
struct kfsw_gpredict_bearing {
	/** Degrees, 0 to 360. */
	int32_t azimuth_mdeg;
	/** Degrees, -90 to 90. Below the horizon is refused. */
	int32_t elevation_mdeg;
};

/** What an operator can see without asking the rotator. */
struct kfsw_gpredict_status {
	/** enum kfsw_gpredict_state */
	uint8_t state;
	/** Bearing last accepted, in millidegrees. */
	int32_t azimuth_mdeg;
	int32_t elevation_mdeg;
	/** Bearing the rotator last reported, in millidegrees. */
	int32_t actual_azimuth_mdeg;
	int32_t actual_elevation_mdeg;
	/** Frequency last accepted from the predictor, in hertz. 0 if none. */
	uint64_t frequency_hz;
	/** Milliseconds since the last bearing, 0 while none has arrived. */
	uint32_t silence_ms;
	/** Grace period in force, in milliseconds. */
	uint32_t grace_ms;
	/** Bearings accepted since boot. */
	uint32_t bearings;
	/** Bearings refused since boot, below the horizon or out of range. */
	uint32_t refusals;
	/** Passes that ended because the predictor went quiet. */
	uint32_t passes_ended;
	/** Faults recorded since boot. */
	uint32_t faults;
	/** Why the last refusal or fault happened, or 0. */
	int last_error;
};

/**
 * @brief Decide the next state.
 *
 * Pure: the same state and event always give the same answer, which is what
 * makes the end of a pass testable without a rotator.
 *
 * @param state Where the tracker is.
 * @param event What happened.
 * @return The state to move to, which may be @p state.
 */
enum kfsw_gpredict_state kfsw_gpredict_next(enum kfsw_gpredict_state state,
					    enum kfsw_gpredict_event event);

/** Human-readable name of a state, for the shell and for a log line. */
const char *kfsw_gpredict_state_name(enum kfsw_gpredict_state state);

/** Start the tracker parked, with the compiled grace period. */
int kfsw_gpredict_init(void);

/**
 * @brief Offer a bearing from the predictor.
 *
 * @retval 0 Accepted; the antenna is tracking.
 * @retval -EINVAL A NULL, or a bearing outside the rotator's range.
 * @retval -EPERM The tracker is in fault and has not been cleared.
 */
int kfsw_gpredict_bearing(const struct kfsw_gpredict_bearing *bearing);

/** Offer a Doppler-corrected frequency. Zero is refused with -EINVAL. */
int kfsw_gpredict_frequency(uint64_t frequency_hz);

/**
 * @brief Ask for the park position now, whatever the pass is doing.
 *
 * @retval 0 On the way to the park position.
 * @retval -EPERM The tracker is in fault. The rotator cannot be trusted to
 *         move, so an operator clears the fault first.
 */
int kfsw_gpredict_park(void);

/** Clear a fault and park. Returns -EALREADY when there is nothing to clear. */
int kfsw_gpredict_clear(void);

/** Read the tracker. Returns -EINVAL for a NULL destination. */
int kfsw_gpredict_get_status(struct kfsw_gpredict_status *status);

/** Parameter table of the pass tracker. */
extern const struct kfsw_param_definition_set kfsw_gpredict_param_definitions;

/**
 * @defgroup kfsw_modules_gpredict_profiles Which spacecraft the rules are for
 * @ingroup kfsw_modules_gpredict
 *
 * The ground proxy stands where the rotator and radio daemons stand and sees
 * every bearing and frequency Gpredict sends. What it cannot know is which
 * spacecraft they are for: the hamlib protocols carry angles and a frequency
 * and nothing else. So the spacecraft is chosen here, by hand, and its profile
 * decides what the proxy is allowed to forward.
 *
 * The profiles live in the node rather than in the proxy because this is where
 * the commands, the parameters and the operator already are. A ground tool that
 * kept its own copy would be a second place to change them, and the two would
 * disagree the first time somebody edited one.
 *
 * @{
 */

/** Longest profile name, including the terminator. */
#define KFSW_GPREDICT_PROFILE_NAME_MAX 16U

/** Wire identifiers of this module's commands. Never reused. */
#define KFSW_COMMAND_ID_GPREDICT_SELECT 40U
#define KFSW_COMMAND_ID_GPREDICT_ENABLE 41U
#define KFSW_COMMAND_ID_GPREDICT_PARK 42U
#define KFSW_COMMAND_ID_GPREDICT_CLEAR 43U

/** What the node does with the frequency Gpredict sends. */
enum kfsw_gpredict_frequency_policy {
	/** Pass it through. Gpredict owns the Doppler. */
	KFSW_GPREDICT_FREQUENCY_FOLLOW = 0,
	/**
	 * Use the profile's own frequency instead. Gpredict keeps correcting
	 * every cycle, so this is a standing override, not a value set once.
	 */
	KFSW_GPREDICT_FREQUENCY_HOLD = 1,
	/** Send nothing to the radio. The rotator is unaffected. */
	KFSW_GPREDICT_FREQUENCY_IGNORE = 2,
};

/** One spacecraft and the rules that apply while it is selected. */
struct kfsw_gpredict_profile {
	char name[KFSW_GPREDICT_PROFILE_NAME_MAX];
	/** Catalogue number, so an operator can check it against Gpredict. */
	uint32_t catalogue;
	/** Travel this profile allows, in millidegrees. */
	int32_t azimuth_min_mdeg;
	int32_t azimuth_max_mdeg;
	int32_t elevation_min_mdeg;
	int32_t elevation_max_mdeg;
	/** The frequency to hold, in hertz. Read only under HOLD. */
	uint64_t frequency_hz;
	/** enum kfsw_gpredict_frequency_policy */
	uint8_t policy;
	/** False leaves the profile defined but unselectable. */
	bool enabled;
};

/**
 * @brief Define a profile at an index, replacing whatever was there.
 *
 * @retval 0 Defined.
 * @retval -EINVAL A NULL, an empty name, a travel outside the rotator's own
 *         limits, an unknown policy, or HOLD with no frequency to hold.
 * @retval -ENOSPC @p index is past the compiled profile count.
 */
int kfsw_gpredict_profile_define(uint8_t index, const struct kfsw_gpredict_profile *profile);

/**
 * @brief Choose the profile whose rules apply.
 *
 * Selecting does not move the antenna: it changes what the next bearing is
 * judged against, and a pass already running keeps its state.
 *
 * @retval 0 Selected.
 * @retval -ENOENT No profile is defined at @p index.
 * @retval -EPERM The profile is defined but not enabled.
 */
int kfsw_gpredict_profile_select(uint8_t index);

/** Enable or disable a profile. Disabling the selected one refuses with -EBUSY. */
int kfsw_gpredict_profile_enable(uint8_t index, bool enabled);

/** Read one profile. Returns -ENOENT for an index that holds nothing. */
int kfsw_gpredict_profile_get(uint8_t index, struct kfsw_gpredict_profile *profile);

/** Index of the selected profile, or -ENOENT when none is. */
int kfsw_gpredict_profile_selected(void);

/**
 * @brief Judge a bearing against the selected profile.
 *
 * This is what the proxy asks before forwarding. It applies the profile's
 * travel on top of the rotator's own limits, so a profile can narrow the
 * travel and never widen it.
 *
 * @retval 0 Allowed, and the tracker has taken it.
 * @retval -ENOENT No profile is selected.
 * @retval -EINVAL Outside the profile's travel or the rotator's.
 * @retval -EPERM The tracker is in fault.
 */
int kfsw_gpredict_profile_bearing(const struct kfsw_gpredict_bearing *bearing);

/**
 * @brief Ask what to send the radio for a frequency Gpredict offered.
 *
 * @param[in] offered What Gpredict sent, in hertz.
 * @param[out] send What to forward, in hertz. Zero means forward nothing.
 * @retval 0 Decided.
 * @retval -ENOENT No profile is selected.
 * @retval -EINVAL A NULL destination, or a zero offer.
 */
int kfsw_gpredict_profile_frequency(uint64_t offered, uint64_t *send);

/** The module's commands, for a composition to register. */
extern const struct kfsw_command_definition_set kfsw_gpredict_command_definitions;

/** @} */

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* KFSW_MODULES_GPREDICT_H */
