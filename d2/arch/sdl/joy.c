/*
 *
 * SDL joystick support
 *
 */

#include <string.h>   // for memset
#include <stdlib.h>   // for abs

#include "joy.h"
#include "key.h"       // for the KEY_* codes joy_menu_key returns
#include "dxxerror.h"
#include "timer.h"
#include "console.h"
#include "event.h"
#include "text.h"
#include "u_mem.h"
#include "playsave.h"
#include "game.h"      // Game_wind: are we flying, or in a menu?
#include "window.h"
#include "kconfig.h"

int num_joysticks = 0;
int joy_num_axes = 0;

/* This struct is a "virtual" joystick, which includes all the axes
 * and buttons of every joystick found.
 */
static struct joyinfo {
	int n_axes;
	int n_buttons;
	int axis_value[JOY_MAX_AXES];
	ubyte button_state[JOY_MAX_BUTTONS];
	ubyte button_last_state[JOY_MAX_BUTTONS]; // for HAT movement only
} Joystick;

typedef struct d_event_joystickbutton
{
	event_type type;
	int button;
} d_event_joystickbutton;

typedef struct d_event_joystick_moved
{
	event_type	type;	// EVENT_JOYSTICK_MOVED
	int		axis;
	int 		value;
} d_event_joystick_moved;

/* This struct is an array, with one entry for each physical joystick
 * found.
 */
static struct {
	SDL_Joystick *handle;
	int n_axes;
	int n_buttons;
	int n_hats;
	int hat_map[MAX_HATS_PER_JOYSTICK];  //Note: Descent expects hats to be buttons, so these are indices into Joystick.buttons
	int axis_map[MAX_AXES_PER_JOYSTICK];
	// Where each axis sits when untouched, sampled at open. Sticks rest at 0,
	// but an XInput trigger rests at the negative extreme - so a deadzone
	// measured from zero reads it as permanently held.
	int axis_rest[MAX_AXES_PER_JOYSTICK];
	int button_map[MAX_BUTTONS_PER_JOYSTICK];
	int axis_button_map[MAX_AXES_PER_JOYSTICK];
} SDL_Joysticks[MAX_JOYSTICKS];


// Start on an XInput-shaped pad, which is what these handhelds present. Only
// used as a fallback, so a player who binds it to something keeps that instead.
#define JOY_BUTTON_START 7

/*
 * True while the cockpit has the focus. Opening the in-game menu puts a window
 * in front of Game_wind, so this correctly says no once a menu is up and the
 * full navigation set is wanted again.
 */
static int joy_flying(void)
{
	return Game_wind != NULL && window_get_front() == Game_wind;
}

/*
 * What a button means in flight. Almost nothing: the pad is for flying, and the
 * game returns unhandled for any button the player has not bound, which would
 * otherwise let the menu translation fire and drop them out of the level.
 */
static int joy_game_key(int button)
{
	if (num_joysticks > 0 && SDL_Joysticks[0].n_buttons > JOY_BUTTON_START
	    && button == SDL_Joysticks[0].button_map[JOY_BUTTON_START])
		return KEY_ESC;

	return 0;
}

/*
 * Send a pad button, and fall back to the keyboard if nothing wanted it.
 *
 * Most of Descent's full-screen states - the briefing, the credits, the score
 * table, the menus - handle EVENT_KEY_COMMAND and EVENT_MOUSE_* and nothing
 * else, so on a handheld with no keyboard there is no way out of them. The ones
 * that do read the pad (the game itself, the automap, the controls screen)
 * return handled and are unaffected, so this cannot double up on them.
 */
static int joy_send_button(int button, event_type type)
{
	d_event_joystickbutton event;
	int handled;

	event.type = type;
	event.button = button;
	con_printf(CON_DEBUG, "Sending event %s, button %d\n",
		(type == EVENT_JOYSTICK_BUTTON_DOWN) ? "EVENT_JOYSTICK_BUTTON_DOWN" : "EVENT_JOYSTICK_BUTTON_UP", button);
	handled = event_send((d_event *)&event);

	if (!handled && type == EVENT_JOYSTICK_BUTTON_DOWN)
	{
		int key = joy_flying() ? joy_game_key(button) : joy_menu_key(button);

		if (key)
		{
			d_event_keycommand keyevent;

			keyevent.type = EVENT_KEY_COMMAND;
			keyevent.keycode = key;
			con_printf(CON_DEBUG, "Nobody took button %d; offering key %d\n", button, key);
			event_send((d_event *)&keyevent);
		}
	}

	return handled;
}

void joy_button_handler(SDL_JoyButtonEvent *jbe)
{
	int button;

	button = SDL_Joysticks[jbe->which].button_map[jbe->button];

	Joystick.button_state[button] = jbe->state;

	joy_send_button(button, (jbe->type == SDL_JOYBUTTONDOWN) ? EVENT_JOYSTICK_BUTTON_DOWN : EVENT_JOYSTICK_BUTTON_UP);
}

void joy_hat_handler(SDL_JoyHatEvent *jhe)
{
	int hat = SDL_Joysticks[jhe->which].hat_map[jhe->hat];
	int hbi;

	//Save last state of the hat-button
	Joystick.button_last_state[hat  ] = Joystick.button_state[hat  ];
	Joystick.button_last_state[hat+1] = Joystick.button_state[hat+1];
	Joystick.button_last_state[hat+2] = Joystick.button_state[hat+2];
	Joystick.button_last_state[hat+3] = Joystick.button_state[hat+3];

	//get current state of the hat-button
	Joystick.button_state[hat  ] = ((jhe->value & SDL_HAT_UP)>0);
	Joystick.button_state[hat+1] = ((jhe->value & SDL_HAT_RIGHT)>0);
	Joystick.button_state[hat+2] = ((jhe->value & SDL_HAT_DOWN)>0);
	Joystick.button_state[hat+3] = ((jhe->value & SDL_HAT_LEFT)>0);

	//determine if a hat-button up or down event based on state and last_state
	for(hbi=0;hbi<4;hbi++)
	{
		if( !Joystick.button_last_state[hat+hbi] && Joystick.button_state[hat+hbi]) //last_state up, current state down
			joy_send_button(hat+hbi, EVENT_JOYSTICK_BUTTON_DOWN);
		else if(Joystick.button_last_state[hat+hbi] && !Joystick.button_state[hat+hbi])  //last_state down, current state up
			joy_send_button(hat+hbi, EVENT_JOYSTICK_BUTTON_UP);
	}
}

int joy_axis_handler(SDL_JoyAxisEvent *jae)
{
	int axis;
	d_event_joystick_moved event;

	axis = SDL_Joysticks[jae->which].axis_map[jae->axis];

	// inaccurate stick is inaccurate. SDL might send SDL_JoyAxisEvent even if the value is the same as before.
	if (Joystick.axis_value[axis] == jae->value/256)
		return 0;

	event.type = EVENT_JOYSTICK_MOVED;
	event.axis = axis;
	event.value = Joystick.axis_value[axis] = jae->value/256;
	con_printf(CON_DEBUG, "Sending event EVENT_JOYSTICK_MOVED, axis: %d, value: %d\n",event.axis, event.value);
	event_send((d_event *)&event);

	return 1;
}

// Deflection from rest can reach twice the nominal range on an axis that rests
// at an extreme, so keep it inside what the rest of the code expects.
static int clamp_axis(int value)
{
	if (value >  127) return  127;
	if (value < -128) return -128;
	return value;
}

int joy_apply_deadzone(int value, int deadzone)
{
	if (value > deadzone)
		return ((value - deadzone) * 128) / (128 - deadzone);
	else if (value < -deadzone)
		return ((value + deadzone) * 128) / (128 - deadzone);
	else
		return 0;
}

static int send_axis_button_event(unsigned button, event_type e)
{
	Joystick.button_state[button] = (e == EVENT_JOYSTICK_BUTTON_UP) ? 0 : 1;
	joy_send_button(button, e);
	return 1;
}

int joy_axisbutton_handler(SDL_JoyAxisEvent *jae)
{
	int button;
	int sent = 0;

	button = SDL_Joysticks[jae->which].axis_button_map[jae->axis];

	// We have to hardcode a deadzone here. It's not mapped into the settings.
	// We could add another deadzone slider called "axis button deadzone".
	// I think it's safe to assume a 30% deadzone on analog button presses for now.
	int deadzone = 38;
	// Measured from the axis's own rest position. An XInput trigger idles at
	// -32767, which is well past any deadzone taken from zero - so without
	// this every trigger reports a button held down from the moment the game
	// starts, and in a menu that jams navigation completely.
	int rest = SDL_Joysticks[jae->which].axis_rest[jae->axis];
	int prev_value = joy_apply_deadzone(clamp_axis(Joystick.axis_value[jae->axis] - rest), deadzone);
	int new_value = joy_apply_deadzone(clamp_axis(jae->value/256 - rest), deadzone);

	if (prev_value <= 0 && new_value >= 0) // positive pressed
	{
		if (prev_value < 0) // Do previous direction release first if the case
			sent |= send_axis_button_event(button + 1, EVENT_JOYSTICK_BUTTON_UP);
		if (new_value > 0)
			sent |= send_axis_button_event(button, EVENT_JOYSTICK_BUTTON_DOWN);
	}
	else if (prev_value >= 0 && new_value <= 0) // negative pressed
	{
		if (prev_value > 0) // Do previous direction release first if the case
			sent |= send_axis_button_event(button, EVENT_JOYSTICK_BUTTON_UP);
		if (new_value < 0)
			sent |= send_axis_button_event(button + 1, EVENT_JOYSTICK_BUTTON_DOWN);
	}

	return sent;
}


/* ----------------------------------------------- */

int joy_menu_key(int button)
{
	int j;

	if (button < 0)
		return 0;

	// Only the first pad drives menus. A second one is for a second player,
	// and having it move the first player's menu would be worse than useless.
	if (num_joysticks < 1)
		return 0;

	// The hat, which joy_init expands into four consecutive buttons in the
	// order up, right, down, left.
	for (j = 0; j < SDL_Joysticks[0].n_hats; j++)
	{
		int hat = SDL_Joysticks[0].hat_map[j];

		if (button == hat)     return KEY_UP;
		if (button == hat + 1) return KEY_RIGHT;
		if (button == hat + 2) return KEY_DOWN;
		if (button == hat + 3) return KEY_LEFT;
	}

	// The first stick, through the synthetic buttons joy_axisbutton_handler
	// makes: each axis becomes two buttons, negative then positive. Axis 0 is
	// left/right and axis 1 is up/down on every pad this is likely to meet.
	if (SDL_Joysticks[0].n_axes > 1)
	{
		int x = SDL_Joysticks[0].axis_button_map[0];
		int y = SDL_Joysticks[0].axis_button_map[1];

		// joy_init labels these "-A" then "+A", but joy_axisbutton_handler
		// sends the base index for POSITIVE deflection and base+1 for
		// negative. Follow what is actually sent: right and down are the
		// positive ends of a stick, so they take the base.
		if (button == x)     return KEY_RIGHT;
		if (button == x + 1) return KEY_LEFT;
		if (button == y)     return KEY_DOWN;
		if (button == y + 1) return KEY_UP;
	}

	// Buttons 0 and 1 are the south and east face buttons on anything
	// XInput-shaped, which is what "confirm" and "cancel" mean to a player.
	if (SDL_Joysticks[0].n_buttons > 0 && button == SDL_Joysticks[0].button_map[0])
		return KEY_ENTER;
	if (SDL_Joysticks[0].n_buttons > 1 && button == SDL_Joysticks[0].button_map[1])
		return KEY_ESC;

	// Start accepts the screen outright. Confirm alone is not enough: on a page
	// of checkboxes it ticks the one under the cursor and never leaves, which
	// is a dead end on the netgame player screen. This comes through as
	// KEY_PADENTER, which newmenu accepts on and which the check/radio remap
	// leaves alone, so it means accept whatever the cursor is sitting on.
	if (num_joysticks > 0 && SDL_Joysticks[0].n_buttons > JOY_BUTTON_START
	    && button == SDL_Joysticks[0].button_map[JOY_BUTTON_START])
		return KEY_PADENTER;

	return 0;
}

/* ----------------------------------------------- */

void joy_init()
{
	int i,j,n;
	char temp[64];

	if (SDL_Init(SDL_INIT_JOYSTICK) < 0) {
		con_printf(CON_NORMAL, "sdl-joystick: initialisation failed: %s.",SDL_GetError());
		return;
	}

	memset(&Joystick,0,sizeof(Joystick));
	memset(joyaxis_text, 0, JOY_MAX_AXES * sizeof(char *));
	memset(joybutton_text, 0, JOY_MAX_BUTTONS * sizeof(char *));

	n = SDL_NumJoysticks();

	if (n >= MAX_JOYSTICKS) {
		Warning("sdl-joystick: found %d joysticks, only %d supported.\n", n, MAX_JOYSTICKS);
		n = MAX_JOYSTICKS;
	} else
		con_printf(CON_NORMAL, "sdl-joystick: found %d joysticks\n", n);

	for (i = 0; i < n; i++) {
#if SDL_VERSION_ATLEAST(2, 0, 0)
		con_printf(CON_NORMAL, "sdl-joystick %d: %s\n", i, SDL_JoystickNameForIndex(i));
#else
		con_printf(CON_NORMAL, "sdl-joystick %d: %s\n", i, SDL_JoystickName(i));
#endif
		SDL_Joysticks[num_joysticks].handle = SDL_JoystickOpen(i);
		if (SDL_Joysticks[num_joysticks].handle) {

			SDL_Joysticks[num_joysticks].n_axes
				= SDL_JoystickNumAxes(SDL_Joysticks[num_joysticks].handle);
			if(SDL_Joysticks[num_joysticks].n_axes > MAX_AXES_PER_JOYSTICK)
			{
				Warning("sdl-joystick: found %d axes, only %d supported.\n", SDL_Joysticks[num_joysticks].n_axes, MAX_AXES_PER_JOYSTICK);
				SDL_Joysticks[num_joysticks].n_axes = MAX_AXES_PER_JOYSTICK;
			}

			SDL_Joysticks[num_joysticks].n_buttons
				= SDL_JoystickNumButtons(SDL_Joysticks[num_joysticks].handle);
			if(SDL_Joysticks[num_joysticks].n_buttons > MAX_BUTTONS_PER_JOYSTICK)
			{
				Warning("sdl-joystick: found %d buttons, only %d supported.\n", SDL_Joysticks[num_joysticks].n_buttons, MAX_BUTTONS_PER_JOYSTICK);
				SDL_Joysticks[num_joysticks].n_buttons = MAX_BUTTONS_PER_JOYSTICK;
			}

			SDL_Joysticks[num_joysticks].n_hats
				= SDL_JoystickNumHats(SDL_Joysticks[num_joysticks].handle);
			if(SDL_Joysticks[num_joysticks].n_hats > MAX_HATS_PER_JOYSTICK)
			{
				Warning("sdl-joystick: found %d hats, only %d supported.\n", SDL_Joysticks[num_joysticks].n_hats, MAX_HATS_PER_JOYSTICK);
				SDL_Joysticks[num_joysticks].n_hats = MAX_HATS_PER_JOYSTICK;
			}

			con_printf(CON_NORMAL, "sdl-joystick: %d axes\n", SDL_Joysticks[num_joysticks].n_axes);
			con_printf(CON_NORMAL, "sdl-joystick: %d buttons\n", SDL_Joysticks[num_joysticks].n_buttons);
			con_printf(CON_NORMAL, "sdl-joystick: %d hats\n", SDL_Joysticks[num_joysticks].n_hats);

			// Sample the resting position before anything is touched, so the
			// axis-to-button conversion below can measure deflection from
			// where an axis actually sits rather than from zero.
			SDL_JoystickUpdate();
			for (j=0; j < SDL_Joysticks[num_joysticks].n_axes; j++)
			{
				int rest = SDL_JoystickGetAxis(SDL_Joysticks[num_joysticks].handle, j) / 256;

				SDL_Joysticks[num_joysticks].axis_rest[j] = rest;
				if (abs(rest) > 64)
					con_printf(CON_NORMAL, "sdl-joystick: axis %d rests at %d - treating as a trigger\n", j, rest);

				sprintf(temp, "J%d A%d", i + 1, j + 1);
				joyaxis_text[Joystick.n_axes] = d_strdup(temp);
				SDL_Joysticks[num_joysticks].axis_map[j] = Joystick.n_axes++;
			}
			for (j=0; j < SDL_Joysticks[num_joysticks].n_buttons; j++)
			{
				sprintf(temp, "J%d B%d", i + 1, j + 1);
				joybutton_text[Joystick.n_buttons] = d_strdup(temp);
				SDL_Joysticks[num_joysticks].button_map[j] = Joystick.n_buttons++;
			}
			for (j=0; j < SDL_Joysticks[num_joysticks].n_hats; j++)
			{
				if (Joystick.n_buttons + 4 > MAX_BUTTONS_PER_JOYSTICK)
					break;
				SDL_Joysticks[num_joysticks].hat_map[j] = Joystick.n_buttons;
				//a hat counts as four buttons
				sprintf(temp, "J%d H%d%c", i + 1, j + 1, 0202);
				joybutton_text[Joystick.n_buttons++] = d_strdup(temp);
				sprintf(temp, "J%d H%d%c", i + 1, j + 1, 0177);
				joybutton_text[Joystick.n_buttons++] = d_strdup(temp);
				sprintf(temp, "J%d H%d%c", i + 1, j + 1, 0200);
				joybutton_text[Joystick.n_buttons++] = d_strdup(temp);
				sprintf(temp, "J%d H%d%c", i + 1, j + 1, 0201);
				joybutton_text[Joystick.n_buttons++] = d_strdup(temp);
			}
			for (j=0; j < SDL_Joysticks[num_joysticks].n_axes; j++)
			{
				if (Joystick.n_buttons + 2 > MAX_BUTTONS_PER_JOYSTICK)
					break;
				SDL_Joysticks[num_joysticks].axis_button_map[j] = Joystick.n_buttons;
				//an axis count as 2 buttons. negative - and positive +
				sprintf(temp, "J%d -A%d", i + 1, j + 1);
				joybutton_text[Joystick.n_buttons++] = d_strdup(temp);
				sprintf(temp, "J%d +A%d", i + 1, j + 1);
				joybutton_text[Joystick.n_buttons++] = d_strdup(temp);
			}

			num_joysticks++;
		}
		else
			con_printf(CON_NORMAL, "sdl-joystick: initialization failed!\n");

		con_printf(CON_NORMAL, "sdl-joystick: %d axes (total)\n", Joystick.n_axes);
		con_printf(CON_NORMAL, "sdl-joystick: %d buttons (total)\n", Joystick.n_buttons);
	}

	joy_num_axes = Joystick.n_axes;
}

void joy_close()
{
	SDL_JoystickClose(SDL_Joysticks[num_joysticks].handle);

	while (Joystick.n_axes--)
		d_free(joyaxis_text[Joystick.n_axes]);
	while (Joystick.n_buttons--)
		d_free(joybutton_text[Joystick.n_buttons]);
}

void event_joystick_get_axis(d_event *event, int *axis, int *value)
{
	Assert(event->type == EVENT_JOYSTICK_MOVED);

	*axis  = ((d_event_joystick_moved *)event)->axis;
	*value = ((d_event_joystick_moved *)event)->value;
}

void joy_flush()
{
	int i;

	if (!num_joysticks)
		return;

	for (i = 0; i < Joystick.n_buttons; i++)
		Joystick.button_state[i] = SDL_RELEASED;
}

int event_joystick_get_button(d_event *event)
{
	Assert((event->type == EVENT_JOYSTICK_BUTTON_DOWN) || (event->type == EVENT_JOYSTICK_BUTTON_UP));
	return ((d_event_joystickbutton *)event)->button;
}
