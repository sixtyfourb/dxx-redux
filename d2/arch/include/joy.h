/*
 *
 * Header for joystick functions
 *
 */

#ifndef _JOY_H
#define _JOY_H

#include "pstypes.h"
#include "fix.h"
#include <SDL.h>

struct d_event;

#define MAX_JOYSTICKS				16
#define MAX_AXES_PER_JOYSTICK		128
#define MAX_BUTTONS_PER_JOYSTICK	128
#define MAX_HATS_PER_JOYSTICK		4
#define JOY_MAX_AXES				(MAX_AXES_PER_JOYSTICK * MAX_JOYSTICKS)
#define JOY_MAX_BUTTONS				(MAX_BUTTONS_PER_JOYSTICK * MAX_JOYSTICKS)

extern int joy_num_axes; // set to Joystick.n_axes. solve different?
extern void joy_init();
extern void joy_close();
extern void event_joystick_get_axis(struct d_event *event, int *axis, int *value);
extern void joy_flush();
extern int event_joystick_get_button(struct d_event *event);
extern void joy_button_handler(SDL_JoyButtonEvent *jbe);
extern void joy_hat_handler(SDL_JoyHatEvent *jhe);
extern int joy_axis_handler(SDL_JoyAxisEvent *jae);
extern int joy_axisbutton_handler(SDL_JoyAxisEvent *jae);

extern int joy_apply_deadzone(int value, int deadzone);

// Menu navigation from a gamepad.
//
// Descent's menus read the keyboard and the mouse and nothing else, so on a
// handheld with no keyboard there is no way to move a selection or back out of
// a screen. This maps a joystick button - including the synthetic buttons a hat
// and the first stick's axes produce - onto the key the menus already
// understand, so the menu code needs to learn nothing about joysticks.
//
// Returns 0 for a button with no menu meaning, which leaves it free for the
// player to bind to a flight action as before.
extern int joy_menu_key(int button);

#endif // _JOY_H
