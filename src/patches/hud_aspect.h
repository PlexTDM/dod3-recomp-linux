/* The HUD in a view of another shape than 16:9 (src/patches/hud_aspect.cpp). */
#pragma once
#include <stdint.h>

/* Bridge 17, from Sqex03GameHUD.DrawHUD (tools/menu_patch.py): phase 1 the
 * HUD begins (canvas: its UCanvas), 0 it is done, 2 a full-screen layer
 * follows, 3 it is drawn. on: the Aspect Ratio setting has the view boxed to
 * a shape of its own; off, the HUD is left as the title draws it. The begin
 * also tells the present the view's shape, every frame the HUD is drawn
 * (rsx_present_set_content_aspect). */
void dod3_hud_aspect(int phase, uint32_t canvas, bool on);

/* Once at boot, after the lifted function table is registered: the hooks
 * that keep the HUD's markers on the view. */
void dod3_hud_aspect_install();
