/* The keyboard and mouse as the pad: src/input/kbm.cpp. */
#pragma once

/* What a key can be bound to: the pad's buttons, then the two sticks' four
 * directions each. */
enum Dod3Bind {
    kBindCross,
    kBindCircle,
    kBindSquare,
    kBindTriangle,
    kBindL1,
    kBindR1,
    kBindL2,
    kBindR2,
    kBindL3,
    kBindR3,
    kBindStart,
    kBindSelect,
    kBindUp,
    kBindDown,
    kBindLeft,
    kBindRight,
    kBindMoveUp,
    kBindMoveDown,
    kBindMoveLeft,
    kBindMoveRight,
    kBindLookUp,
    kBindLookDown,
    kBindLookLeft,
    kBindLookRight,
    kBinds
};

/* Once at boot. Nothing where the port has no keyboard and mouse of its own
 * yet (the toolkit's built-in keys stay). */
void dod3_kbm_install();
/* Is the port's keyboard and mouse the one in use on this platform? */
bool dod3_kbm_present();
/* The first key bound to it, as the settings file names keys (the button
 * prompts draw it); NULL when nothing is. */
const char* dod3_kbm_key(Dod3Bind b);
/* Is the mouse the camera (so the right stick's prompt is the mouse)? */
bool dod3_kbm_mouse_look();
