/*
 * Android touch/gamepad input bridge - engine side.
 *
 * Holds the state the OpenTouch layer pushes in, and folds it into
 * control_info once per frame from kconfig_read_controls(). Doing it there (and
 * before the per-frame clamp) means touch input goes through exactly the same
 * limits as keyboard, mouse and joystick.
 */

#include <string.h>

#include "pstypes.h"
#include "maths.h"
#include "game.h"
#include "kconfig.h"
#include "segment.h"
#include "object.h"
#include "automap.h"
#include "window.h"
#include "player.h"
#include "timer.h"

#include "touch_input.h"

#ifdef DXX_TOUCH_DEBUG
#include <android/log.h>
#define TLOG(...) __android_log_print(ANDROID_LOG_INFO, "DxxTouch", __VA_ARGS__)
#else
#define TLOG(...) do {} while (0)
#endif

/* Full stick deflection maps to the engine's per-frame maximum, so the clamp in
 * kconfig_read_controls() is what ultimately bounds the rate. */
#define AXIS_MAX_ROT_PITCH(ft)  ((ft) / 2)
#define AXIS_MAX_ROT(ft)        (ft)
#define AXIS_MAX_THRUST(ft)     (ft)

/* Swipe/gyro deltas arrive as a fraction of the screen (TouchJoy emits
 * last.x - finger.x in normalised coordinates, so a brisk drag is ~0.02 per
 * event). The engine's mouse path does mouse_axis = pixels * FrameTime / 8 and
 * then heading_time += mouse_axis * MouseSens / 8, i.e. 8 pixels of movement is
 * one full frame of turn. Scaling by FrameTime the same way keeps the feel
 * framerate-independent; the gain sets how far you drag for a full-rate turn
 * (1/40 of the screen here). The look/turn sensitivity sliders multiply on top. */
#define REL_GAIN                40.0f

/* How long a relative (swipe/gyro) delta stays live, matching how the engine
 * holds mouse deltas - see the EVENT_IDLE branch of kconfig_read_controls(). */
#define REL_HOLD_TIME           (F1_0 / 30)

/* Axis gain lives in dxx_game_interface.cpp, where the touch layer's own
 * scaling is known; these only cover the gamepad's own -1..1 axes. */
#define GAIN_FORWARD            1.0f
#define GAIN_SIDEWAYS           1.0f
#define GAIN_VERTICAL           1.0f

static struct
{
	/* absolute stick deflections, -1..1, held until changed */
	float forward, sideways, vertical;
	float pitch, heading, bank;

	/* relative deltas, consumed each frame */
	float pitch_rel, heading_rel, bank_rel;

	ubyte action_state[DXX_TA_MAX];
	ubyte action_prev_state[DXX_TA_MAX];
	ubyte action_count[DXX_TA_MAX];

	int select_weapon;
} touch;

static float clampf(float v)
{
	if (v > 1.0f)
		return 1.0f;
	if (v < -1.0f)
		return -1.0f;
	return v;
}

void dxx_touch_axis_forward(float v)
{
	touch.forward = clampf(v * GAIN_FORWARD);
}

void dxx_touch_axis_sideways(float v)
{
	touch.sideways = clampf(v * GAIN_SIDEWAYS);
}

void dxx_touch_axis_vertical(float v)
{
	touch.vertical = clampf(v * GAIN_VERTICAL);
}

void dxx_touch_axis_pitch(float v, int relative)
{
	if (relative)
		touch.pitch_rel += v;
	else
		touch.pitch = clampf(v);
}

void dxx_touch_axis_heading(float v, int relative)
{
	if (relative)
		touch.heading_rel += v;
	else
		touch.heading = clampf(v);
}

void dxx_touch_axis_bank(float v, int relative)
{
	if (relative)
		touch.bank_rel += v;
	else
		touch.bank = clampf(v);
}

void dxx_touch_action(int state, int action)
{
	if (action < 0 || action >= DXX_TA_MAX)
		return;

	/* Count edges, not frames: the game consumes *_count and expects one per
	 * press. */
	if (state && !touch.action_state[action])
		touch.action_count[action]++;

	touch.action_state[action] = state ? 1 : 0;
}

void dxx_touch_select_weapon(int number_key)
{
	/* Keys 1-9 select weapons 1-9, key 0 selects weapon 10. */
	touch.select_weapon = number_key ? number_key : 10;
}

int dxx_touch_screen_mode(void)
{
	/* Nothing on screen yet: the engine has not created any window. */
	if (!window_get_front())
		return DXX_TS_BLANK;

	if (Automap_active)
		return DXX_TS_MAP;

	if (Game_wind && window_get_front() == Game_wind)
		return DXX_TS_GAME;

	return DXX_TS_MENU;
}

static void apply_state(int action, ubyte *dest)
{
	if (touch.action_state[action])
		*dest = 1;
	else if (touch.action_prev_state[action])
		*dest = 0;	/* we set it last time round: release it */
}

static void apply_count(int action, ubyte *dest)
{
	if (touch.action_count[action])
	{
		*dest += touch.action_count[action];
		touch.action_count[action] = 0;
	}
}

void dxx_touch_apply_controls(void)
{
	static fix64 rel_clear_time = 0;
	fix ft = FrameTime;
	int i;

	if (timer_query() >= rel_clear_time)
	{
		touch.pitch_rel = touch.heading_rel = touch.bank_rel = 0;
		rel_clear_time = timer_query() + REL_HOLD_TIME;
	}

	/* --- Rotation --- */
	Controls.pitch_time += (fix) (touch.pitch * AXIS_MAX_ROT_PITCH(ft));
	Controls.heading_time += (fix) (touch.heading * AXIS_MAX_ROT(ft));
	Controls.bank_time += (fix) (touch.bank * AXIS_MAX_ROT(ft));

	TLOG("apply: rel p=%f h=%f | abs p=%f h=%f | ft=%d pitch_time=%d heading_time=%d",
	     touch.pitch_rel, touch.heading_rel, touch.pitch, touch.heading,
	     (int) ft, (int) Controls.pitch_time, (int) Controls.heading_time);

	Controls.pitch_time += (fix) (touch.pitch_rel * REL_GAIN * ft);
	Controls.heading_time += (fix) (touch.heading_rel * REL_GAIN * ft);
	Controls.bank_time += (fix) (touch.bank_rel * REL_GAIN * ft);

	/* --- Translation --- */
	Controls.forward_thrust_time += (fix) (touch.forward * AXIS_MAX_THRUST(ft));
	Controls.sideways_thrust_time += (fix) (touch.sideways * AXIS_MAX_THRUST(ft));
	Controls.vertical_thrust_time += (fix) (touch.vertical * AXIS_MAX_THRUST(ft));

	/* Button pairs for the axes the sticks do not cover. */
	if (touch.action_state[DXX_TA_SLIDE_UP])
		Controls.vertical_thrust_time += AXIS_MAX_THRUST(ft);
	if (touch.action_state[DXX_TA_SLIDE_DOWN])
		Controls.vertical_thrust_time -= AXIS_MAX_THRUST(ft);
	if (touch.action_state[DXX_TA_SLIDE_RIGHT])
		Controls.sideways_thrust_time += AXIS_MAX_THRUST(ft);
	if (touch.action_state[DXX_TA_SLIDE_LEFT])
		Controls.sideways_thrust_time -= AXIS_MAX_THRUST(ft);
	if (touch.action_state[DXX_TA_BANK_LEFT])
		Controls.bank_time += AXIS_MAX_ROT(ft);
	if (touch.action_state[DXX_TA_BANK_RIGHT])
		Controls.bank_time -= AXIS_MAX_ROT(ft);
	if (touch.action_state[DXX_TA_ACCELERATE])
		Controls.forward_thrust_time += AXIS_MAX_THRUST(ft);
	if (touch.action_state[DXX_TA_REVERSE])
		Controls.forward_thrust_time -= AXIS_MAX_THRUST(ft);

	/* --- Buttons --- */
	apply_state(DXX_TA_FIRE_PRIMARY, &Controls.fire_primary_state);
	apply_count(DXX_TA_FIRE_PRIMARY, &Controls.fire_primary_count);
	apply_state(DXX_TA_FIRE_SECONDARY, &Controls.fire_secondary_state);
	apply_count(DXX_TA_FIRE_SECONDARY, &Controls.fire_secondary_count);
	apply_count(DXX_TA_FIRE_FLARE, &Controls.fire_flare_count);
	apply_count(DXX_TA_DROP_BOMB, &Controls.drop_bomb_count);
	apply_count(DXX_TA_CYCLE_PRIMARY, &Controls.cycle_primary_count);
	apply_count(DXX_TA_CYCLE_SECONDARY, &Controls.cycle_secondary_count);

	apply_state(DXX_TA_AUTOMAP, &Controls.automap_state);
	apply_count(DXX_TA_AUTOMAP, &Controls.automap_count);
	apply_state(DXX_TA_REAR_VIEW, &Controls.rear_view_state);
	apply_count(DXX_TA_REAR_VIEW, &Controls.rear_view_count);

#ifdef DXX_GAME_D2
	apply_state(DXX_TA_AFTERBURNER, &Controls.afterburner_state);
	apply_count(DXX_TA_HEADLIGHT, &Controls.headlight_count);
	apply_state(DXX_TA_ENERGY_SHIELD, &Controls.energy_to_shield_state);
	apply_count(DXX_TA_TOGGLE_BOMB, &Controls.toggle_bomb_count);
#endif

	if (touch.select_weapon)
	{
		Controls.select_weapon_count = touch.select_weapon;
		touch.select_weapon = 0;
	}

	for (i = 0; i < DXX_TA_MAX; i++)
		touch.action_prev_state[i] = touch.action_state[i];
}
