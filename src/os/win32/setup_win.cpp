/*
 * First-run setup, Windows: the installer (src/ui/wizard.cpp, in a
 * Direct3D 12 window from src/ui/ui_win.cpp) when something the game
 * needs is missing, or when asked for (--setup); once the game is installed,
 * the launcher (src/ui/launcher.cpp) at each start, unless dod3.ini turns it
 * off (Shift held at start opens it anyway). dod3_setup_win() returns 0 to
 * start the game.
 */
#include "install/install.h"
#include "ui/ui.h"

#include <windows.h>

extern "C" int dod3_setup_win(const wchar_t* base_dir, int force)
{
    using namespace dod3setup;
    const fs::path base(base_dir);
    const bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;   /* read first: it is soon let go of */
    if (!installed(base).ready() || force) {
        const int r = run_installer(base);
        if (r >= 0) return r;
        /* no window (no Direct3D 12): the command line does the same */
        MessageBoxW(NULL,
                    L"Setup could not open its window (it needs Direct3D 12).\n\n"
                    L"The game can be installed from a command prompt instead, in this folder:\n\n"
                    L"dod3.exe --install <disc .iso or folder> <update .pkg> [DLC .pkg ...]\n\n"
                    L"Details are in dod3.log.",
                    L"Drakengard 3 Recompiled", MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
        return 1;
    }
    if (!launcher_wanted(base, shift)) return 0;
    for (;;) {
        const int r = run_launcher(base);
        if (r != 2) return r < 0 ? 0 : r;   /* no window: the game starts as before */
        /* Game Files: the installer, then back to the launcher (its Start
         * Game starts the game) */
        const int i = run_installer(base);
        if (i == 0) return 0;
        if (!installed(base).ready()) return 1;
    }
}
