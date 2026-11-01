/* The trophy listener: what the game's trophy unlocks are reported to
 * (ps3recomp's sceNpTrophy reads the disc's trophy pack, keeps the unlocks
 * and tells us, trophy_host.h). The Vulkan backend has no present-time
 * overlay yet, so the listener writes each unlock to the log. */
#pragma once

/* main.cpp, before the game starts: the layer, the trophy listener, and the
 * sync's client name. */
void dod3_overlay_install();
