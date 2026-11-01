# Drakengard 3 Recompiled -- Linux

A native Linux port of Drakengard 3 (PlayStation 3), made by recompiling the
game's own code. It does not include the game: you need your own copy.

This project is not affiliated with or endorsed by Square Enix, Access Games
or Sony. Drakengard 3 and all of its assets belong to their owners.

## Requirements

- 64-bit Linux, SDL2, Vulkan and a Vulkan-capable driver.
- A CPU supporting AVX2, FMA and BMI2 for the default x86-64-v3 build.
- The US release of Drakengard 3 (BLUS31197), dumped to a folder, a
  decrypted disc image, or the PSN package (NPUB31251).
- The genuine 1.01 update package. DLC packages are optional.

An ordinary encrypted disc image and other regions are not supported. No
emulator, console firmware or package licence files are needed.

## Installing

Install with the native Linux executable, adding any DLC packages after the
update package:

```sh
DOD3_INSTALL_BASE="$HOME/.local/share/dod3-recomp" \
    dod3 --install <decrypted-disc.iso-or-folder> <update.pkg> [DLC.pkg ...]
DOD3_INSTALL_BASE="$HOME/.local/share/dod3-recomp" dod3
```

By default, game files, settings, saves, shader cache and logs are under
`$XDG_DATA_HOME/dod3-recomp`, or `~/.local/share/dod3-recomp` when
`XDG_DATA_HOME` is unset. Set `DOD3_INSTALL_BASE` to use another data
directory. The native game has no graphical launcher. The separate optional
`dod3-setup` target is a Linux GUI installer for the Windows release under
Wine or Proton; it is not needed for the native game.

## Settings

In the game, Settings > Graphics Settings sets resolution, display mode,
frame rate, v-sync, anti-aliasing, texture filtering and field of view.
System Settings controls intro skipping, behavior when unfocused, the final
song's auto-play, controller prompts, mouse sensitivity and the blood on
screen. Advanced Graphics controls aspect ratio, shadows, motion blur,
post-processing, depth of field, draw distance and tree detail.

Settings can also be changed in `dod3.ini` in the install directory:

| Setting | What it does |
|---|---|
| `RSX_SCALE` | Internal resolution as a multiple of 1280x720; values above the window resolution enable supersampling |
| `RSX_DISPLAY` | `windowed`, `borderless` or `fullscreen` |
| `RSX_WINDOW` | Window size in windowed mode, e.g. `1920x1080` |
| `RSX_VSYNC` | `1` syncs to the display; `0` allows the game to exceed its refresh rate |
| `DOD3_FPS` | Frame cap; `60` is tested, `120` works, `adaptive` selects a rate from 30 to 120 |
| `DOD3_FOV` | Additional gameplay camera field of view, in degrees |
| `DOD3_ASPECT` | `16:10`, `21:9`, `32:9`, a ratio such as `2.39`, or `auto` |
| `RSX_ANISO` | Texture filtering, from 1x to 16x |
| `RSX_AA` | `fxaa`, `msaa2`, `msaa4` or `msaa8`; unset disables anti-aliasing |
| `DOD3_SHADOWS` | `high` or `ultra` for sharper shadows |
| `DOD3_MOTION_BLUR` | `0` disables motion blur |
| `DOD3_POSTFX` | `0` disables depth of field, bloom and color grading |
| `DOD3_DOF` | `0` disables only the depth of field; the bloom stays |
| `DOD3_SCREEN_BLOOD` | `border` leaves out the blood splashes and keeps the low-health border; `0` leaves out both |
| `DOD3_DRAW_DISTANCE` | `2`, `3`, `5` or `10` for increased scenery distance |
| `DOD3_TREE_LOD` | `2`, `3`, `5` or `10` for increased tree distance and detail |
| `DOD3_SKIP_INTRO` | `1` skips the logos and opening movie; `0` keeps them |
| `DOD3_UNFOCUSED` | `mute` silences audio when unfocused; `pause` also pauses the game |
| `DOD3_SONG_AUTOPLAY` | `1` plays branch D's final song for you: every note is pressed on its time (the RPCS3 patch "Auto Play Song of The D Branch Final Mission", by bucanero). In the game: Settings > System Settings > Final Song Auto-Play; unset is off |
| `DOD3_PROMPTS` | `ps3`, `ps4`, `ps5`, `xbox`, `switch` or `keyboard`; unset follows the active controller |
| `DOD3_MOUSE_SENS` | Mouse camera sensitivity; `DOD3_MOUSE=0` disables mouse camera control |
| `KEY_CROSS`, `KEY_MOVE_UP`, ... | Keys for each pad button and stick direction; the file lists all supported keys |
| `DOD3_CAMERA_SPEED` | `original` restores the game's original camera speed behavior |
| `DOD3_CHEAT_HEALTH` | `1` keeps your health full (Zero's or the dragon's); enemies are hurt as usual |
| `DOD3_CHEAT_INTONER` | `1` fills the Intoner gauge at the first blood and keeps it full |
| `DOD3_CHEAT_ITEMS` | `1` stops items being used up |
| `DOD3_CHEAT_TIME` | `1` stops a mission's time limit counting down |

## Cheats

The cheats come from a code list for the PS3 game and are off unless asked
for. Each takes effect at the next start; put them in `dod3.ini` (or the
environment):

```ini
DOD3_CHEAT_HEALTH = 1
DOD3_CHEAT_INTONER = 1
DOD3_CHEAT_ITEMS = 1
DOD3_CHEAT_TIME = 1
```

- **Health**: your health stays full -- Zero's, or the dragon's in the sky.
  Enemies are hurt as usual.
- **Intoner gauge**: the gauge fills at the first blood and stays full, in
  Intoner mode too.
- **Items**: items are not used up.
- **Time limits**: a mission's time limit does not count down.

`DOD3_CHEAT_LOG=1` says what each wrapped call was asked for.

## Controls

Xbox, PlayStation (DualShock 4 and DualSense) and Switch Pro controllers are
supported over USB or Bluetooth. The first connected controller is player 1.
The game can also be played with keyboard and mouse.

| Key | Button | In play |
|---|---|---|
| W A S D | Left stick | Move |
| Mouse | Right stick | Camera |
| Left mouse button | Square | Attack |
| Right mouse button | Triangle | Strong attack |
| Space | Cross | Jump; confirm in menus |
| E, Backspace, Esc | Circle | Guard; back in menus |
| Shift | R1 | Evade |
| Q, middle mouse button | L1 | Lock on |
| Mouse wheel | Right stick left/right | Switch target |
| R, F | L2, R2 | Weapon menus |
| V | L3 and R3 together | Intoner Mode |
| Arrow keys, 1 2 3 4 | D-pad | Menus; items |
| Enter | Start | Pause |
| Tab | Select | Select menu |

Keys are configurable in `dod3.ini`. Escape is always the back button: it
closes a menu or steps back, and closes the window only if you ask it to
(`PS3RECOMP_ESCAPE_QUITS=1` in `dod3.ini` or the environment). To quit, close
the window or press Alt+F4. When the game has focus the cursor is hidden and
the mouse controls the camera.

In the sky the dragon aims with a cursor on the screen; while it is shown the
mouse moves that cursor, and only what goes past the screen's edge turns the
view (`DOD3_CURSOR_LOG=1` says when it does).

Started outside Steam on a Steam Deck (Desktop Mode), the port leaves the
keyboard and mouse alone and uses the pad only, since Steam's desktop layout
turns the Deck's buttons into keys (and every press came twice). Set
`DOD3_KEYBOARD=1` for a keyboard of your own.

## Japanese voices and mods

With the Japanese Voice Pack installed, choose Change Outfits/Music > Voice
> Language: Japanese on the title menu. The official pack does not include
Japanese for the six DLC chapters. The DLC Undub mod provides those voices.

The Linux game has no graphical mod manager. Mods must be installed manually
in the corresponding game data folders. For example, a patch mod can be
placed under `game/disc/game/BLES00000/USRDIR/PATCH`. A mod that replaces
`SQEX03GAME.XXX` may remove the port's additions to the in-game Settings
menu. Back up the install directory before modifying game files.

## Saves and troubleshooting

Back up the `gamedata` folder in the install directory to preserve saves.
The `cache` folder contains compiled shaders and may be removed to rebuild
them.

Each run writes `dod3.log` in the install directory and keeps the previous
log as `dod3.prev.log`. Include the log, GPU and CPU details when reporting
problems. If the game says its executable cannot be loaded, reinstall the
genuine 1.01 update package with `dod3 --install`.

At aspect ratios other than 16:9, some HUD elements may remain out of place.
