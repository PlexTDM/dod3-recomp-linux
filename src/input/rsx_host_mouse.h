#ifndef DOD3_RSX_HOST_MOUSE_H
#define DOD3_RSX_HOST_MOUSE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void rsx_host_mouse_capture(int on);
int rsx_host_mouse_captured(void);
void rsx_host_mouse_take_motion(int* dx, int* dy);
int rsx_host_mouse_take_wheel(void);
void rsx_host_window_escape_closes(int on);
void rsx_host_mouse_attach(void* window);
int rsx_host_mouse_message(void* window, unsigned message, uintptr_t wp, intptr_t lp, intptr_t* result);

#ifdef __cplusplus
}
#endif

#endif
