#!/usr/bin/env python3
"""Make the window icon from the icon's source: src/res/icon.ico -> src/res/icon.png.

The 256x256 entry of src/res/icon.ico becomes src/res/icon.png, which
CMakeLists.txt embeds as gen/window_icon.h (the launcher's window) and
tools/package_linux.sh ships beside dod3 for the desktop entry. Regenerate
after replacing the .ico:

    python3 tools/make_window_icon.py [source.ico] [size]

Needs Pillow (developer side only: the PNG it writes is committed).
"""
import os
import sys

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "src/res/icon.ico")
OUT = os.path.join(ROOT, "src/res/icon.png")


def main(argv):
    src = argv[1] if len(argv) > 1 else SRC
    size = int(argv[2]) if len(argv) > 2 else 256

    with Image.open(src) as ico:
        sizes = sorted(ico.info.get("sizes", {ico.size}), key=lambda s: s[0])
        pick = next((s for s in sizes if s[0] >= size), sizes[-1])
        ico.size = pick                # the ICO plugin keeps every entry; choose one
        ico.load()
        im = ico.convert("RGBA")
    if im.size != (size, size):
        im = im.resize((size, size), Image.Resampling.LANCZOS)
    im.save(OUT)
    print(f"{OUT}: {im.width}x{im.height} from {src} ({pick[0]}x{pick[1]})")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
