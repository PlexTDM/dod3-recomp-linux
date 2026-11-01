/* The trophy listener (ui/overlay.h). There is no present-time layer on the
 * Vulkan backend yet, so a trophy is only written to the log. */
#include "ui/overlay.h"

#include "trophy_host.h"   /* ps3recomp: the unlocks, as the game makes them */

#include <cstdio>
#include <cstdlib>

namespace {

void on_unlock(const TrophyUnlockEvent* e, void*)
{
    fprintf(stderr, "[trophy] %s%s: %s -- %s\n", e->preview ? "(preview) " : "", trophy_grade_name(e->grade), e->name,
            e->detail);
}

}  // namespace

void dod3_overlay_install()
{
    const char* e = getenv("DOD3_TROPHY_POPUPS");
    const bool popups = !(e && e[0] == '0');
    trophy_host_set_listener(on_unlock, popups ? (void*)1 : nullptr);
}
