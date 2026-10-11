/* The button prompts in another pad's glyphs: src/patches/prompts.cpp. */
#pragma once

/* At start-up: the renderer asks from then on. */
void dod3_prompts_install();
/* DOD3_PROMPTS, player 1's pad or RSX_SCALE may have changed: called from the
 * frame clock (it looks four times a second) and by the settings page (now). */
void dod3_prompts_poll(bool now = false);
