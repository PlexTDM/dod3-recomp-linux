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

    with Image.open(src) as im:
        # An .ico keeps every size; take the smallest entry at least `size` (or
        # the largest) through the ICO plugin's own accessor. Assigning
        # Image.size to choose one works at run time, but Pillow's stubs mark
        # it read-only.
        ico = getattr(im, "ico", None)
        if ico is None:
            frame, picked = im, im.size
        else:
            entries = sorted(ico.sizes(), key=lambda s: s[0])
            picked = next((s for s in entries if s[0] >= size), entries[-1])
            frame = ico.getimage(picked)
        image = frame.convert("RGBA")
    if image.size != (size, size):
        image = image.resize((size, size), Image.Resampling.LANCZOS)
    image.save(OUT)
    print(f"{OUT}: {image.width}x{image.height} from {src} ({picked[0]}x{picked[1]})")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
