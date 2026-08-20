/*
 * Android messagebox implementation using SDL2's built-in message box.
 */

#include <SDL.h>
#include "messagebox.h"

void msgbox_warning(char *message)
{
	SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING, "Warning", message, NULL);
}

void msgbox_error(const char *message)
{
	SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Error", message, NULL);
}
