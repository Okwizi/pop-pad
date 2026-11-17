#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build the icon atlases pop-pad loads, from folders of 128 px PNGs.

    build_glyphs.py                           pack every style under assets/glyphs
    build_glyphs.py --pack assets/vendor/kenney
                                              first re-pick the shipped styles from Kenney's
                                              Input Prompts pack (CC0), then pack them
    build_glyphs.py --icons my_icons --name mine
                                              pack your own folder as the style "mine"
                                              (select it with Style=mine in poppad.ini)

A style is a folder holding one PNG per input the game knows for an Xbox-type pad, named as in
INPUTS below (a.png, dpad_up.png, lstick_left.png ...). The atlas is ICON_COLUMNS x ICON_ROWS
tiles of ICON_TILE pixels in that order, written as an uncompressed top-down 32-bit TGA to
dist/poppad/glyphs/<style>.tga. The numbers must match src/icons.h; tests/ checks that they do.
"""

import argparse
import struct
import sys
from pathlib import Path

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
GLYPHS = ROOT / "assets" / "glyphs"
DIST = ROOT / "dist" / "poppad" / "glyphs"
TILE, COLUMNS, ROWS = 128, 8, 4

# The game's own order: its input codes 52..75 for the "Microsoft XInput-style controller".
INPUTS = [
    "dpad_up",
    "dpad_down",
    "dpad_left",
    "dpad_right",
    "start",
    "back",
    "ls",
    "rs",
    "lb",
    "rb",
    "a",
    "b",
    "x",
    "y",
    "lt",
    "rt",
    "lstick_up",
    "lstick_down",
    "lstick_left",
    "lstick_right",
    "rstick_up",
    "rstick_down",
    "rstick_left",
    "rstick_right",
]


def directions(prefix):
    names = {f"dpad_{d}": f"{prefix}_dpad_{d}" for d in ("up", "down", "left", "right")}
    for side, stick in (("l", "lstick"), ("r", "rstick")):
        for d in ("up", "down", "left", "right"):
            names[f"{stick}_{d}"] = f"{prefix}_stick_{side}_{d}"
    names["ls"] = f"{prefix}_stick_l_press"
    names["rs"] = f"{prefix}_stick_r_press"
    return names


def xbox(face):
    names = directions("xbox")
    names.update({b: face.format(b) for b in "abxy"})
    names.update(
        lb="xbox_lb",
        rb="xbox_rb",
        lt="xbox_lt",
        rt="xbox_rt",
        start="xbox_button_menu",
        back="xbox_button_view",
    )
    return "Xbox Series", names


def playstation():
    names = directions("playstation")
    names.update(
        a="playstation_button_color_cross",
        b="playstation_button_color_circle",
        x="playstation_button_color_square",
        y="playstation_button_color_triangle",
        lb="playstation_trigger_l1",
        rb="playstation_trigger_r1",
        lt="playstation_trigger_l2",
        rt="playstation_trigger_r2",
        start="playstation5_button_options",
        back="playstation5_button_create",
        ls="playstation_button_l3",
        rs="playstation_button_r3",
    )
    return "PlayStation Series", names


def switch():
    names = directions("switch")
    # Nintendo's letters sit in the opposite positions: the bottom button (XInput A) is B.
    names.update(
        a="switch_button_b",
        b="switch_button_a",
        x="switch_button_y",
        y="switch_button_x",
        lb="switch_button_l",
        rb="switch_button_r",
        lt="switch_button_zl",
        rt="switch_button_zr",
        start="switch_button_plus",
        back="switch_button_minus",
    )
    return "Nintendo Switch", names


# Where each shipped style's pictures come from in Kenney's pack: (pack folder, file per input).
STYLES = {
    "xbox": xbox("xbox_button_color_{}"),
    # Xbox letter positions without Xbox colours: 8BitDo and other third-party XInput pads.
    "generic": xbox("xbox_button_{}"),
    "playstation": playstation(),
    "switch": switch(),
}


def bleed(rgba, passes=6):
    """Spread colour into fully transparent pixels.

    The game filters the texture, which mixes in the colour of transparent neighbours; left
    black, they would put a dark rim around every icon.
    """
    rgb = rgba[..., :3].astype(np.float32)
    known = rgba[..., 3] > 0
    for _ in range(passes):
        total = np.zeros_like(rgb)
        count = np.zeros(known.shape, np.float32)
        for dy, dx in ((0, 1), (0, -1), (1, 0), (-1, 0)):
            shifted_known = np.roll(known, (dy, dx), (0, 1))
            total += np.roll(rgb, (dy, dx), (0, 1)) * shifted_known[..., None]
            count += shifted_known
        fill = ~known & (count > 0)
        rgb[fill] = total[fill] / count[fill][:, None]
        known |= fill
    rgb[~known] = 255
    out = rgba.copy()
    out[..., :3] = rgb.round().astype(np.uint8)
    return out


def tga_bytes(rgba):
    """An uncompressed, top-down, 32-bit BGRA TGA, the layout glyphs_init() checks for."""
    height, width = rgba.shape[:2]
    header = struct.pack("<BBBHHBHHHHBB", 0, 0, 2, 0, 0, 0, 0, 0, width, height, 32, 0x28)
    return header + rgba[..., [2, 1, 0, 3]].tobytes()


def load_style(folder):
    """The 24 icons of a style folder, in INPUTS order."""
    folder = Path(folder)
    missing = [name for name in INPUTS if not (folder / f"{name}.png").is_file()]
    if missing:
        raise SystemExit(f"{folder}: missing {', '.join(m + '.png' for m in missing)}")
    images = []
    for name in INPUTS:
        image = Image.open(folder / f"{name}.png").convert("RGBA")
        if image.size != (TILE, TILE):
            raise SystemExit(f"{folder / (name + '.png')}: {image.width}x{image.height}, expected {TILE}x{TILE}")
        images.append(image)
    return images


def build_atlas(images):
    atlas = Image.new("RGBA", (TILE * COLUMNS, TILE * ROWS), (0, 0, 0, 0))
    for i, image in enumerate(images):
        atlas.paste(image, (TILE * (i % COLUMNS), TILE * (i // COLUMNS)))
    return np.array(atlas)


def build_preview(images):
    sheet = Image.new("RGBA", (TILE * COLUMNS, TILE * 3), (40, 34, 28, 255))
    for i, image in enumerate(images):
        sheet.alpha_composite(image, (TILE * (i % COLUMNS), TILE * (i // COLUMNS)))
    return sheet


def pick_from_pack(pack):
    """Copy the shipped styles' pictures out of Kenney's pack into assets/glyphs/<style>/."""
    for style, (folder, names) in STYLES.items():
        target = GLYPHS / style
        target.mkdir(parents=True, exist_ok=True)
        for key in INPUTS:
            source = Path(pack) / folder / "Double" / f"{names[key]}.png"
            Image.open(source).convert("RGBA").save(target / f"{key}.png")
        print(f"{style}: {len(INPUTS)} glyphs picked from {folder}")


def pack_style(name, folder, previews):
    images = load_style(folder)
    DIST.mkdir(parents=True, exist_ok=True)
    (DIST / f"{name}.tga").write_bytes(tga_bytes(bleed(build_atlas(images))))
    if previews:
        build_preview(images).save(GLYPHS / f"preview-{name}.png")
    print(f"{name}: {len(images)} icons -> {DIST / (name + '.tga')}")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--pack", metavar="DIR", help="Kenney Input Prompts pack to re-pick the shipped styles from")
    parser.add_argument("--icons", metavar="DIR", help="a folder of your own icons to pack as one style")
    parser.add_argument("--name", help="style name for --icons (Style=<name> in poppad.ini)")
    args = parser.parse_args(argv)

    if args.icons:
        if not args.name:
            parser.error("--icons needs --name")
        pack_style(args.name, args.icons, previews=False)
        return 0
    if args.pack:
        pick_from_pack(args.pack)
    styles = sorted(p.name for p in GLYPHS.iterdir() if p.is_dir())
    if not styles:
        parser.error(f"no style folders under {GLYPHS}")
    for style in styles:
        pack_style(style, GLYPHS / style, previews=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
