#!/bin/bash
# Put the port in the desktop's application menu, with its icon in the dock,
# the taskbar and the window's titlebar.
#
#   tools/install_desktop_entry.sh [path/to/dod3]     install (default: dod3
#                                                     beside this script)
#   tools/install_desktop_entry.sh --remove           take it out again
#
# Nothing is installed system-wide: the entry goes to
# ~/.local/share/applications and the icon to ~/.local/share/icons/hicolor, so
# it appears for this user only. The entry points at where dod3 is now: move
# the folder and run it again.
set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd -P)
apps=${XDG_DATA_HOME:-$HOME/.local/share}/applications
icons=${XDG_DATA_HOME:-$HOME/.local/share}/icons/hicolor
entry=$apps/dod3-recomp.desktop
icon_dst=$icons/256x256/apps/dod3-recomp.png

die() { echo "install_desktop_entry: $*" >&2; exit 2; }

if [ "${1:-}" = "--remove" ]; then
    rm -f "$entry" "$icon_dst"
    command -v update-desktop-database >/dev/null 2>&1 && update-desktop-database -q "$apps" || true
    echo "install_desktop_entry: removed $entry"
    exit 0
fi

exe=${1:-}
if [ -z "$exe" ]; then
    if [ -x "$here/dod3" ]; then
        exe=$here/dod3                # the release: this script sits beside it
    else
        exe=$(command -v dod3 || true)
    fi
fi
[ -n "$exe" ] || die "no dod3 found: give its path, e.g. $0 ~/Games/dod3-recomp/dod3"
[ -x "$exe" ] || die "$exe is not executable"
exe=$(readlink -f "$exe")
dir=$(dirname "$exe")

# The icon: the release ships one beside dod3; in the source tree it is
# src/res/icon.png.
if [ -f "$dir/icon.png" ]; then
    icon=$dir/icon.png
elif [ -f "$here/../src/res/icon.png" ]; then
    icon=$here/../src/res/icon.png
else
    icon=
fi

mkdir -p "$apps" "$(dirname "$icon_dst")"
if [ -n "$icon" ]; then
    cp -f "$icon" "$icon_dst"
else
    echo "install_desktop_entry: no icon.png found; the entry will use a blank icon" >&2
fi

cat > "$entry" <<EOF
[Desktop Entry]
Type=Application
Version=1.0
Name=Drakengard 3 Recompiled
GenericName=Drakengard 3
Comment=Play Drakengard 3 (PlayStation 3) from your own copy of the game
Exec=$exe
Icon=dod3-recomp
Terminal=false
Categories=Game;ActionGame;
StartupNotify=true
StartupWMClass=dod3-recomp
EOF

command -v update-desktop-database >/dev/null 2>&1 && update-desktop-database -q "$apps" || true
command -v gtk-update-icon-cache >/dev/null 2>&1 && gtk-update-icon-cache -q -t "$icons" || true

echo "install_desktop_entry: $entry -> $exe"
[ -n "$icon" ] && echo "install_desktop_entry: icon at $icon_dst"
echo "install_desktop_entry: it shows in the menu after a moment; remove it with $0 --remove"
