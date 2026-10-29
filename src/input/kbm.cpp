/*
 * The keyboard and mouse as the pad.
 *
 * The pad library asks this for port 0's state at every sweep
 * (cellPad_set_host_input), beside whatever real pad is there. The host
 * backend does not currently feed it key or mouse state, so every entry point
 * below is inert: the pad is still usable, the keyboard bindings in dod3.ini
 * have nothing to drive them, and the prompts fall back to the pad's glyphs.
 *
 * The binding table, the key-name parser and the per-platform key polling went
 * with the platform runners this build no longer carries. A Linux
 * implementation belongs on SDL2 (the window and input subsystem the Vulkan
 * backend already uses); until then this translation unit keeps the interface
 * linkable and the call sites unchanged.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>
#include "input/camera.h"
#include "input/kbm.h"
#include "rsx_host_mouse.h"
#include "util.h"


void dod3_kbm_install() {}
bool dod3_kbm_present() { return false; }
bool dod3_kbm_mouse_look() { return false; }
const char* dod3_kbm_key(Dod3Bind) { return nullptr; }
