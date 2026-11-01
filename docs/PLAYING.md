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
`XDG_DATA_HOME` is unset. `DOD3_GAMEDATA_ROOT` moves the saves alone.

**The install folder is remembered.** When an install finishes -- the
launcher's Game Files page or `dod3 --install` -- the folder it went to is
written to `dod3.ini` as `DOD3_INSTALL_BASE`, and every start reads it back,
so the game is found whatever directory you start it from and wherever the
program is unpacked. To use another folder, either give it at install time:

```sh
DOD3_INSTALL_BASE=~/Games/drakengard-data dod3 --install \
    <disc-or-folder> <update.pkg> [DLC.pkg ...]
```

or set `DOD3_INSTALL_BASE = /home/you/Games/drakengard-data` in `dod3.ini`
and move the `game`, `gamedata` and `elf` folders there (the launcher shows
which folder it is using, under Game Files). The environment variable wins
over the file for one run; if the recorded folder has gone, the program says
so and falls back to the default.

Starting `dod3` from a folder that holds the game's files (`game/disc/...`)
uses that folder too -- that is how a build tree is played, and it is why the
launcher's key art appears when the game is where you start it.

## The launcher

`dod3` with no arguments opens the launcher: **Play**, **Mods**, **DLC**,
**Trophies**, **Settings**, **Quit**, over the game's own key art. Game Files (at
the bottom) runs the installer, so a fresh download can be installed from the
window: the first Play with nothing installed opens it too.

- Settings here edits `dod3.ini` -- the same rows as the game's own Settings
  menu, plus the ones only the file has (window size, mouse camera, camera
  speed, whether this launcher opens).
- Trophies shows the 72 trophies with their icons, grades, chapters and the
  dates you earned them (see [Trophies](#trophies)).
- Mods shows the fan translations and the DLC undub that are installed, with a
  switch to turn one off and on, Remove, and Add Files / Add Folder.
- DLC lists the 20 packs and adds more.

Mouse, keyboard and controller all work; the game's own Settings menu has the
same options while playing.

To start without it: `DOD3_LAUNCHER = 0` in `dod3.ini` (Settings > System >
Launcher at Start, or edit the file), or `DOD3_LAUNCHER=0 dod3` for one run.
`dod3 --launcher` opens it anyway and `dod3 --setup` goes straight to the
installer.

## The icon

The game's window carries its own icon, and `tools/install_desktop_entry.sh`
(in the release: `install-desktop-entry.sh`) adds a menu entry with the same
icon, so the dock and the taskbar show it and the game is listed as a game:

```sh
~/Games/dod3-recomp/install-desktop-entry.sh        # the dod3 beside it
~/Games/dod3-recomp/install-desktop-entry.sh --remove
```

It writes `~/.local/share/applications/dod3-recomp.desktop` and the icon
under `~/.local/share/icons/hicolor`, for the current user only; move the
folder and run it again.

## Settings

In the game, Settings > Graphics Settings sets resolution, display mode,
frame rate, v-sync, anti-aliasing, texture filtering and field of view.
System Settings controls intro skipping, behavior when unfocused, the final
song's auto-play, controller prompts, mouse sensitivity and the blood on
screen. Advanced Graphics controls aspect ratio, shadows, motion blur,
post-processing, depth of field, draw distance and tree detail. Picture
Settings controls HDR and the post-processing quality.

Settings can also be changed in `dod3.ini` in the install directory:

| Setting                         | What it does                                                                                                                                                                                                                           |
| ------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `RSX_SCALE`                     | Internal resolution as a multiple of 1280x720; values above the window resolution enable supersampling                                                                                                                                 |
| `RSX_DISPLAY`                   | `windowed`, `borderless` or `fullscreen`                                                                                                                                                                                               |
| `RSX_WINDOW`                    | Window size in windowed mode, e.g. `1920x1080`                                                                                                                                                                                         |
| `RSX_VSYNC`                     | `1` syncs to the display; `0` allows the game to exceed its refresh rate                                                                                                                                                               |
| `DOD3_FPS`                      | Frame cap; `60` is tested, `120` works, `adaptive` selects a rate from 30 to 120                                                                                                                                                       |
| `DOD3_INSTALL_BASE`             | The folder the game's files are in; the installer writes it, and every start reads it back (the environment variable of the same name wins for one run)                                                                                |
| `DOD3_FOV`                      | Additional gameplay camera field of view, in degrees                                                                                                                                                                                   |
| `DOD3_ASPECT`                   | `16:10`, `21:9`, `32:9`, a ratio such as `2.39`, or `auto`                                                                                                                                                                             |
| `RSX_ANISO`                     | Texture filtering, from 1x to 16x                                                                                                                                                                                                      |
| `RSX_AA`                        | `fxaa`, `msaa2`, `msaa4` or `msaa8`; unset disables anti-aliasing                                                                                                                                                                      |
| `DOD3_SHADOWS`                  | `high` or `ultra` for sharper shadows                                                                                                                                                                                                  |
| `DOD3_MOTION_BLUR`              | `0` disables motion blur                                                                                                                                                                                                               |
| `DOD3_POSTFX`                   | `0` disables depth of field, bloom and color grading                                                                                                                                                                                   |
| `DOD3_DOF`                      | `0` disables only the depth of field; the bloom stays                                                                                                                                                                                  |
| `RSX_HDR`                       | `1` shows the game in HDR where the display and compositor offer an HDR surface                                                                                                                                                        |
| `DOD3_POSTFX_QUALITY`           | `high` draws the depth of field and bloom smoothly above 720p, with the port's own version of the game's post-processing shader; from the next start                                                                                   |
| `RSX_HDR_WHITE`                 | White in HDR, in nits: `100` to `400`; unset is 200                                                                                                                                                                                    |
| `RSX_HDR_PEAK`                  | The brightest highlight, in nits; unset is 1000                                                                                                                                                                                        |
| `RSX_HDR_HIGHLIGHTS`            | `0` keeps the HDR picture within white                                                                                                                                                                                                 |
| `RSX_HDR_GAMMA`                 | `srgb` uses the sRGB curve instead of a 2.2 power                                                                                                                                                                                      |
| `DOD3_SCREEN_BLOOD`             | `border` leaves out the blood splashes and keeps the low-health border; `0` leaves out both                                                                                                                                            |
| `DOD3_DRAW_DISTANCE`            | `2`, `3`, `5` or `10` for increased scenery distance                                                                                                                                                                                   |
| `DOD3_TREE_LOD`                 | `2`, `3`, `5` or `10` for increased tree distance and detail                                                                                                                                                                           |
| `DOD3_SKIP_INTRO`               | `1` skips the logos and opening movie; `0` keeps them                                                                                                                                                                                  |
| `DOD3_UNFOCUSED`                | `mute` silences audio when unfocused; `pause` also pauses the game                                                                                                                                                                     |
| `DOD3_SONG_AUTOPLAY`            | `1` plays branch D's final song for you: every note is pressed on its time (the RPCS3 patch "Auto Play Song of The D Branch Final Mission", by bucanero). In the game: Settings > System Settings > Final Song Auto-Play; unset is off |
| `DOD3_PROMPTS`                  | `ps3`, `ps4`, `ps5`, `xbox`, `switch` or `keyboard`; unset follows the active controller                                                                                                                                               |
| `DOD3_MOUSE_SENS`               | Mouse camera sensitivity; `DOD3_MOUSE=0` disables mouse camera control                                                                                                                                                                 |
| `KEY_CROSS`, `KEY_MOVE_UP`, ... | Keys for each pad button and stick direction; the file lists all supported keys                                                                                                                                                        |
| `DOD3_CAMERA_SPEED`             | `original` restores the game's original camera speed behavior                                                                                                                                                                          |
| `DOD3_CHEAT_HEALTH`             | `1` keeps your health full (Zero's or the dragon's); enemies are hurt as usual                                                                                                                                                         |
| `DOD3_CHEAT_INTONER`            | `1` fills the Intoner gauge at the first blood and keeps it full                                                                                                                                                                       |
| `DOD3_CHEAT_ITEMS`              | `1` stops items being used up                                                                                                                                                                                                          |
| `DOD3_CHEAT_TIME`               | `1` stops a mission's time limit counting down                                                                                                                                                                                         |

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

## HDR

On a display that can show HDR, and a compositor that offers an HDR surface
(on Wayland, one that exports HDR or gamescope; many X11 desktops offer only
SDR), the game can use it: Settings > Picture Settings > HDR in the game, or
`RSX_HDR = 1` in `dod3.ini`. The log says which was picked:

```sh
grep 'HDR:' dod3.log     # "scRGB FP16" or "HDR10 (10-bit PQ)", or why it stays SDR
```

The game's picture is made for an SDR screen; in HDR it is shown as an SDR
screen would show it -- the same colours and the same dark shadows -- at the
brightness HDR Brightness sets for white, and the light the game itself clips
at white (the sky, fire, embers, glints on blades) can go on above it, up to
your display's peak, rolled off smoothly instead of cut. The HUD, menus and
movies stay at the white you set.

- HDR Brightness: Auto is 200 nits. A lower white leaves the highlights more
  room above it; 200 to 300 nits is a good start in a dim room.
- `RSX_HDR_PEAK` caps the highlights at your display's peak, in nits.
- Switching HDR on changes the picture at once; the brighter highlights come
  from the port's own version of the game's post-processing shader
  (`shaders/override`), which also writes the highlight plane. They need HDR
  on when the game starts; switched on later, the picture is HDR without
  them until the next start. `RSX_HDR_HIGHLIGHTS = 0` keeps the picture
  within white.
- HDR is new here, and untested on an HDR display: reports are welcome.

## Post-processing quality

Settings > Picture Settings > Post-Processing Quality, or
`DOD3_POSTFX_QUALITY = high` in `dod3.ini` (from the next start): above
1280x720 the depth of field and bloom are drawn smoothly, and what is out of
focus stays soft instead of showing sharp detail through the blur. It is the
port's own version of the game's post-processing shader
(`shaders/override/a3e8d0b0476b7c09.hlsl`). At 1280x720 the picture is the
original's; unset is the original at any resolution.

## Trophies

The game's 72 trophies are earned as on the console, read from your copy's own
trophy list, and kept in `gamedata/trophies` (NPWR04682.json) with your saves:
back that folder up with them. Nothing goes to PSN or anywhere else.

- **Trophies, in the launcher**: the launcher's **Trophies** page shows the
  whole list -- each trophy's icon, its grade and what it is for -- grouped by
  chapter (the DLC's included), with how far along you are and a count for
  each grade. Earned ones carry the date; hidden ones stay a question mark
  until earned (Show Hidden reveals them). All / Earned / Not Earned filter
  the list, and L1/R1 switch the filter with a pad.
- In the game, trophies are unlocked and saved; a trophy's unlock is written
  to the log, and no pop-up is drawn over the game yet.
- On the command line, `dod3 --trophies` lists them all -- grade, the date you
  earned each and its name (a hidden trophy keeps its secret until earned):

  ```sh
  build/dod3 --trophies
  ```

- `PS3_TROPHY_PREVIEW=<id>[,<id>]` shows those pop-ups at boot without
  recording anything (on a platform that draws them).

## Controls

Xbox, PlayStation (DualShock 4 and DualSense) and Switch Pro controllers are
supported over USB or Bluetooth. The first connected controller is player 1.
The game can also be played with keyboard and mouse.

| Key                    | Button                 | In play                |
| ---------------------- | ---------------------- | ---------------------- |
| W A S D                | Left stick             | Move                   |
| Mouse                  | Right stick            | Camera                 |
| Left mouse button      | Square                 | Attack                 |
| Right mouse button     | Triangle               | Strong attack          |
| Space                  | Cross                  | Jump; confirm in menus |
| E, Backspace, Esc      | Circle                 | Guard; back in menus   |
| Shift                  | R1                     | Evade                  |
| Q, middle mouse button | L1                     | Lock on                |
| Mouse wheel            | Right stick left/right | Switch target          |
| R, F                   | L2, R2                 | Weapon menus           |
| V                      | L3 and R3 together     | Intoner Mode           |
| Arrow keys, 1 2 3 4    | D-pad                  | Menus; items           |
| Enter                  | Start                  | Pause                  |
| Tab                    | Select                 | Select menu            |

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
> Japanese for the six DLC chapters. The DLC Undub mod provides those voices.

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
