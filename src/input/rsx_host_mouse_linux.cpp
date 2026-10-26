#include "rsx_host_mouse.h"

#include <SDL.h>

extern "C" void rsx_host_mouse_capture(int on)
{
    SDL_SetRelativeMouseMode(on ? SDL_TRUE : SDL_FALSE);
}

extern "C" int rsx_host_mouse_captured(void)
{
    return SDL_GetRelativeMouseMode() == SDL_TRUE;
}

extern "C" void rsx_host_mouse_take_motion(int* dx, int* dy)
{
    int x = 0, y = 0;
    SDL_GetRelativeMouseState(&x, &y);
    if (dx) *dx = x;
    if (dy) *dy = y;
}

extern "C" int rsx_host_mouse_take_wheel(void)
{
    return 0;
}

extern "C" void rsx_host_window_escape_closes(int)
{
}

extern "C" void rsx_host_mouse_attach(void*)
{
}

extern "C" int rsx_host_mouse_message(void*, unsigned, uintptr_t, intptr_t, intptr_t*)
{
    return 0;
}
