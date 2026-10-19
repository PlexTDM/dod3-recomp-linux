# Drakengard 3 Recompiled -- playing it

A native PC port of Drakengard 3 (PlayStation 3), made by recompiling the
game's own code. It does not include the game: you need your own copy, and
the setup takes the game's files from it.

This project is not affiliated with or endorsed by Square Enix, Access Games
or Sony. Drakengard 3 and its assets belong to their owners.


## What you need

1. A computer:

   - Windows 10 or 11, 64-bit, a GPU with Direct3D 12, and a CPU with AVX2
     (Intel Haswell / AMD Zen or newer).
   - Or a Mac with Apple silicon (M1 or newer) on macOS 13 Ventura or newer.

   About 16 GB of free disk space for the game, plus 10 GB more if you
   install the Japanese voice pack. The install folder should be on a
   drive that takes links between folders (NTFS or APFS, not exFAT).

2. Your copy of Drakengard 3, US release (BLUS31197), as one of:

   - the disc dumped to a folder (the one that contains PS3_GAME), or
   - a DECRYPTED disc image (.iso).

   The easiest way is PS3 Disc Dumper (https://github.com/13xforever/ps3-disc-dumper,
   free, for Windows, macOS and Linux) with a compatible Blu-ray drive: it
   reads your disc and writes a decrypted folder, which the setup takes as
   it is. A modded PS3 (multiMAN, webMAN MOD) can also copy the disc to a
   decrypted folder or image. An image ripped with an ordinary disc tool is
   still encrypted and will be refused, and so are other regions (EU, JP).

3. The game's 1.01 update, as the package file a PlayStation 3 downloads:
   UP0082-BLUS31197_00-DOD3PATCH0000000-A0101-V0101-PE.pkg (172 MB). The
   port runs the updated game, so the update is required. The setup checks
   it is the genuine 1.01 package.

4. Optionally, the DLC packages you own (the .pkg files your PlayStation 3
   downloads for them, UP0082-NPUB31251_00-...): the Japanese Voice Pack,
   the six prologue chapters, the garbs and weapons, the music packs. No
   licence files are needed.

You do not need a PlayStation 3 emulator, firmware, or any other tool: the
setup unpacks the packages and prepares the game's executable itself.


## Installing

Windows:

1. Unzip the release anywhere you like (not inside Program Files).
2. Run dod3.exe. Windows may show a SmartScreen warning the first time,
   because the program is not signed: choose "More info", then "Run anyway".
3. The setup opens. Add your files -- the disc image or folder, the update
   package, and any DLC packages -- with Add Files / Add Folder; each one is
   recognised by itself, in any order. Then Install. The game's files are
   copied next to dod3.exe into the "game" folder (a few minutes).
4. Start Game. The first minutes of the first launch stutter while graphics
   shaders are compiled; they are kept in the "cache" folder, so later
   launches are smooth.

macOS:

1. Unzip the release and move "Drakengard 3 Recompiled.app" to
   Applications (or anywhere you like).
2. The first time, macOS refuses to open it, because the app is not
   notarised by Apple. On macOS 15 and later: try to open it once, then go
   to System Settings > Privacy & Security and choose "Open Anyway". On
   macOS 13 and 14: Control-click the app, choose Open, then Open again.
   After that it opens normally.
3. The setup opens, as on Windows. The game's files go into
   ~/Library/Application Support/Drakengard 3 Recompiled.

To run the setup again -- to add DLC later, or to replace a file -- run
dod3.exe --setup from a command prompt on Windows, or hold Option while
opening the app on a Mac. Everything already installed stays.

If the setup cannot open its window, it can run from a command prompt:

    dod3.exe --install <disc .iso or folder> <update .pkg> [DLC .pkg ...]

and `dod3.exe --check <file>...` says what the setup makes of each file
without installing anything.


## Settings

In the game: Settings > Graphics Settings (on the title menu, next to
Audio Settings) sets the resolution, display mode, frame rate, v-sync,
anti-aliasing (FXAA, or MSAA 2x/4x/8x), texture filtering and field of
view. Settings > System Settings sets whether the logos and opening movie
are skipped, what the game does while its window is in the background
(keep running, mute, or pause), which controller's buttons the game shows,
the mouse's sensitivity, and the renderer. Settings > Advanced
Graphics sets the aspect ratio and the engine's own shadow quality, motion
blur, post-processing, draw distance and tree detail. Confirm with X to save
them; everything but the renderer, Skip Intro and Shadow Quality changes
at once (a second of black while the resolution or display mode switches;
Tree Detail in the next area the game loads), those three the next time
the game starts.

The first start adds the pages to the game's menu, which takes a second or
two. The game's own files are not changed.

Or edit dod3.ini (next to dod3.exe; on a Mac, in the Application Support
folder above). It explains each setting:

| Setting | What it does |
|---|---|
| RSX_SCALE | Internal resolution as a multiple of 1280x720: 1.5 = 1080p, 2 = 1440p, 3 = 4K. Above the window's size it is averaged down: supersampling |
| RSX_DISPLAY | windowed, borderless (recommended for full screen) or fullscreen |
| RSX_WINDOW | Window size in windowed mode, e.g. 1920x1080 |
| RSX_VSYNC | 1 to sync to the display; 0 lets the frame rate exceed its refresh |
| DOD3_FPS | Frame-rate cap: 60 is tested and recommended, 120 works; adaptive holds the highest rate from 120 down to 30 that the scene keeps up with; unset keeps the original 30 |
| DOD3_FOV | Field of view: 5 to 30 degrees added to the gameplay camera's 65; cutscenes keep their framing. Unset is the original |
| DOD3_ASPECT | Aspect ratio: auto (the window's), 21:9, 32:9 or a ratio such as 2.39. Wider shows more at the sides in play and cutscenes; a 16:9 screen shows it with bars above and below. The view is 1280 game pixels wide, so raise RSX_SCALE to keep a wider one sharp. The HUD keeps its 16:9 layout for now. Unset is 16:9 |
| RSX_ANISO | Texture filtering: 1 (original) to 16 (16x anisotropic, the default) |
| RSX_AA | fxaa, or msaa2 / msaa4 / msaa8 (MSAA costs a lot at 4K); unset is off |
| DOD3_SHADOWS | high or ultra: sharper, more defined shadows (ultra costs a few percent); unset is the original. From the next start |
| DOD3_MOTION_BLUR | 0 turns motion blur off |
| DOD3_POSTFX | 0 turns off depth of field, bloom and the colour grading |
| DOD3_DRAW_DISTANCE | 2, 3, 5 or 10: props, bushes and small trees are drawn from that many times as far away; unset is the original |
| DOD3_TREE_LOD | 2, 3, 5 or 10: the large trees are drawn, and stay their full models, that many times as far away; unset is the original. From the next area loaded |
| DOD3_SKIP_INTRO | 1 (default) skips the logos and the opening movie at start-up; 0 keeps them |
| DOD3_UNFOCUSED | While the window is in the background: unset keeps running, mute silences the audio, pause stops the game until you come back |
| DOD3_PROMPTS | The buttons the game shows: unset follows the controller you hold; or ps3, ps4, ps5, xbox, switch, keyboard |
| DOD3_MOUSE_SENS | How far the mouse turns the camera; 1 is the default. DOD3_MOUSE = 0 turns the mouse off |
| KEY_CROSS, KEY_MOVE_UP, ... | Windows: the keys for each pad button and stick direction (the file lists them all) |
| DOD3_CAMERA_SPEED | original gives the camera stick the game's own speed, which falls as the frame rate rises; unset keeps it at the console's |
| RSX_BACKEND | Windows: vulkan for the Vulkan renderer (experimental); unset is Direct3D 12 |


## Controls

Xbox, PlayStation (DualShock 4, DualSense) and Switch Pro controllers,
wired or over Bluetooth, plugged in before or during the game; the first
one connected is player 1. The buttons follow their positions, so the
bottom face button is Cross whatever the pad prints on it.

The game shows the buttons of the controller you hold: A and B on an Xbox
pad, B and A on a Switch one, the DualShock 4's or the DualSense's own, the
keys when you play on the keyboard. It changes when you plug another pad in
or pick up the other of pad and keyboard;
Settings > System Settings > Button Prompts fixes it to one (PlayStation 3
is the game's original art). A few places still name the PlayStation 3
pad whatever you choose: "Press the START Button" on the title screen,
"Press START to Skip" in cutscenes, the Circle on the dragon's icon, and
the picture on the controller settings page.

The camera stick turns the camera at the speed it has on the console
whatever the frame rate. The game itself slows it as the frame rate rises
(a quarter of the speed at 120 frames a second); DOD3_CAMERA_SPEED =
original in dod3.ini gives that back. The game's own Controller Settings
(camera sensitivity, inverted axes, the chase camera) work as before.

### Keyboard and mouse

The mouse turns the camera and the keys are the pad's buttons, with or
without a controller plugged in: use either at any moment, and the prompts
on screen follow the one you touched last.

| Key | Button | In play |
|---|---|---|
| W A S D | Left stick | Move |
| Mouse | Right stick | Camera |
| Left mouse button | Square | Attack |
| Right mouse button | Triangle | Strong attack |
| Space | Cross | Jump; confirm in menus |
| E, Backspace | Circle | Guard; back in menus |
| Shift | R1 | Evade |
| Q, middle mouse button | L1 | Lock on |
| Mouse wheel | Right stick left and right | Switch target |
| R, F | L2, R2 | Weapon menus |
| V | L3 and R3 together | Intoner Mode (C and X press one each) |
| Arrow keys, 1 2 3 4 | D-pad | Menus; items |
| Esc, Enter | Start | Pause |
| Tab | Select | The select menu |

Every key can be changed in dod3.ini, which lists them, and the prompts
show the keys you chose. Settings > System Settings > Mouse Sensitivity
sets how far the mouse turns the camera; the game's own camera settings
apply to the mouse as they do to the stick. Escape pauses, so it no longer
closes the game: Alt+F4 does, or Cmd+Q on a Mac.

While the game is in front the cursor is hidden and the mouse is the
game's; switch to another application (Alt+Tab, Cmd+Tab on a Mac) and it
is yours again. On a Mac the keys are places on the keyboard, the same on
every layout; Alt is the Option key, and CapsLock cannot be used.


## Japanese voices

With the Japanese Voice Pack installed: on the title menu, Change
Outfits/Music > Voice > Language: Japanese, then start or continue a game.
Voices, lip-sync and the cutscene movies switch to Japanese.


## Your saves, and updating

Saves and the game's own data are in the "gamedata" folder next to
dod3.exe (on a Mac, in the Application Support folder). Back that folder up
to keep your progress.

To update to a new release, unzip it over the old one, or copy these from
the old folder: game (the installed game), gamedata (saves), dod3.ini (your
settings) and cache (compiled shaders; optional). To uninstall, delete the
folder.


## If something goes wrong

- Each run writes dod3.log next to dod3.exe (the run before is kept as
  dod3.prev.log; on a Mac both are in the Application Support folder).
  Please include it when you report a problem, with your GPU and CPU.
- The setup refuses a file it cannot use and says why: an encrypted disc
  image, another region or version, a package for another game, a damaged
  package (its checksum fails). `dod3.exe --check <file>` shows the same.
- "The game's executable could not be loaded": run dod3.exe --setup and add
  the update package again.


## Known issues

- Tested on few machines so far (Windows: i9-13900K with an RTX 4090;
  macOS: M1 Pro). Reports from other hardware are welcome.
