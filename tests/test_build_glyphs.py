# SPDX-License-Identifier: GPL-3.0-or-later
"""The icon atlas the DLL loads: its layout, its file format and the tool that writes it."""

import numpy as np
import pytest
from PIL import Image

import build_glyphs as bg

SHIPPED_STYLES = ["generic", "playstation", "switch", "xbox"]


def solid_set(folder, colour=(200, 40, 40, 255)):
    folder.mkdir(parents=True, exist_ok=True)
    for name in bg.INPUTS:
        Image.new("RGBA", (bg.TILE, bg.TILE), colour).save(folder / f"{name}.png")
    return folder


@pytest.mark.parametrize("style", SHIPPED_STYLES)
def test_shipped_style_has_every_icon_with_something_drawn(style):
    images = bg.load_style(bg.GLYPHS / style)
    assert len(images) == len(bg.INPUTS) == 24
    for name, image in zip(bg.INPUTS, images, strict=True):
        visible = np.count_nonzero(np.array(image)[..., 3] > 0)
        assert visible > 200, f"{style}/{name}.png is blank"


def test_inputs_are_unique_and_fit_the_atlas():
    assert len(set(bg.INPUTS)) == len(bg.INPUTS)
    assert len(bg.INPUTS) <= bg.COLUMNS * bg.ROWS


def test_each_icon_lands_in_its_own_tile():
    images = bg.load_style(bg.GLYPHS / "xbox")
    atlas = bg.build_atlas(images)
    assert atlas.shape == (bg.TILE * bg.ROWS, bg.TILE * bg.COLUMNS, 4)
    for i, image in enumerate(images):
        x, y = bg.TILE * (i % bg.COLUMNS), bg.TILE * (i // bg.COLUMNS)
        assert np.array_equal(atlas[y : y + bg.TILE, x : x + bg.TILE], np.array(image))
    spare = atlas[bg.TILE * 3 :, :]
    assert not spare[..., 3].any(), "tiles beyond the 24 icons must stay empty"


def test_tga_has_the_layout_the_dll_insists_on():
    rgba = np.zeros((bg.TILE * bg.ROWS, bg.TILE * bg.COLUMNS, 4), np.uint8)
    rgba[0, 0] = (10, 20, 30, 40)
    tga = bg.tga_bytes(rgba)
    width, height = bg.TILE * bg.COLUMNS, bg.TILE * bg.ROWS
    # glyphs_init(): uncompressed true-colour, 32 bits, top-left origin, exact size.
    assert len(tga) == 18 + width * height * 4
    assert tga[2] == 2
    assert tga[16] == 32
    assert tga[17] & 0x20
    assert tga[12] | tga[13] << 8 == width
    assert tga[14] | tga[15] << 8 == height
    assert tga[18:22] == bytes([30, 20, 10, 40]), "pixels are stored blue, green, red, alpha"


def test_bleed_colours_the_fringe_without_touching_alpha_or_visible_pixels():
    rgba = np.zeros((9, 9, 4), np.uint8)
    rgba[4, 4] = (10, 120, 250, 255)
    out = bg.bleed(rgba)
    assert np.array_equal(out[..., 3], rgba[..., 3])
    assert tuple(out[4, 4]) == (10, 120, 250, 255)
    assert tuple(out[4, 5, :3]) == (10, 120, 250), "a transparent neighbour takes the colour"
    assert tuple(out[0, 0, :3]) == (255, 255, 255), "far from any ink it is white, not black"


def test_missing_and_misshapen_icons_are_reported(tmp_path):
    folder = solid_set(tmp_path / "set")
    (folder / "rb.png").unlink()
    with pytest.raises(SystemExit, match=r"missing rb\.png"):
        bg.load_style(folder)

    folder = solid_set(tmp_path / "other")
    Image.new("RGBA", (64, 64)).save(folder / "a.png")
    with pytest.raises(SystemExit, match=r"64x64, expected 128x128"):
        bg.load_style(folder)


def test_your_own_icons_can_be_packed_as_a_style(tmp_path, monkeypatch):
    monkeypatch.setattr(bg, "DIST", tmp_path / "out")
    folder = solid_set(tmp_path / "mine")
    assert bg.main(["--icons", str(folder), "--name", "mine"]) == 0
    tga = (tmp_path / "out" / "mine.tga").read_bytes()
    assert len(tga) == 18 + bg.TILE * bg.COLUMNS * bg.TILE * bg.ROWS * 4
    with pytest.raises(SystemExit):
        bg.main(["--icons", str(folder)])


def test_every_shipped_style_maps_every_input():
    for style, (_folder, names) in bg.STYLES.items():
        assert set(names) >= set(bg.INPUTS), f"{style} lacks {set(bg.INPUTS) - set(names)}"
    assert sorted(bg.STYLES) == SHIPPED_STYLES


def test_switch_swaps_the_letters_because_nintendo_puts_them_the_other_way_round():
    _, names = bg.STYLES["switch"]
    assert names["a"] == "switch_button_b"
    assert names["b"] == "switch_button_a"
    assert names["x"] == "switch_button_y"
    assert names["y"] == "switch_button_x"


@pytest.mark.skipif(not (bg.ROOT / "assets" / "vendor" / "kenney").is_dir(), reason="Kenney's pack is not unpacked")
def test_every_picture_named_in_the_pack_table_exists():
    pack = bg.ROOT / "assets" / "vendor" / "kenney"
    for _style, (folder, names) in bg.STYLES.items():
        for key in bg.INPUTS:
            assert (pack / folder / "Double" / f"{names[key]}.png").is_file()
