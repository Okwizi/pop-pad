# SPDX-License-Identifier: GPL-3.0-or-later
"""Constants and settings that live in more than one place must say the same thing.

The DLL is C++ and the icon tool is Python, so nothing but these tests notices when one side
changes and the other does not.
"""

import configparser
import re

import build_glyphs as bg


def enum_values(text):
    values = {}
    for body in re.findall(r"enum\s*\{(.*?)\}", text, re.S):
        values.update({name: int(number) for name, number in re.findall(r"(\w+)\s*=\s*(\d+)\b(?!\s*[*+])", body)})
    return values


def test_atlas_layout_matches_between_the_dll_and_the_tool(root):
    c = enum_values((root / "src" / "icons.h").read_text())
    assert c["ICON_COUNT"] == len(bg.INPUTS)
    assert c["ICON_COLUMNS"] == bg.COLUMNS
    assert c["ICON_ROWS"] == bg.ROWS
    assert c["ICON_TILE"] == bg.TILE


def test_icons_follow_the_games_input_numbering(root):
    first = enum_values((root / "src" / "icons.h").read_text())["FIRST_PAD_CODE"]
    # Codes the game uses for an Xbox-type pad; src/profile_fix.cpp names them in its comments.
    known = {56: "start", 57: "back", 60: "lb", 61: "rb", 62: "a", 63: "b", 64: "x", 65: "y", 66: "lt", 67: "rt"}
    for code, name in known.items():
        assert bg.INPUTS[code - first] == name


def test_default_bindings_use_inputs_that_have_icons(root):
    text = (root / "src" / "profile_fix.cpp").read_text()
    table = re.search(r"DEFAULTS\[\]\s*=\s*\{(.*?)\n\};", text, re.S).group(1)
    pairs = [(int(a), int(c)) for a, c in re.findall(r"\{(\d+),\s*(\d+)\}", table)]
    first = enum_values((root / "src" / "icons.h").read_text())["FIRST_PAD_CODE"]
    assert len(pairs) == 22
    assert len({action for action, _ in pairs}) == len(pairs), "an action is bound twice"
    for _action, code in pairs:
        assert first <= code < first + len(bg.INPUTS), f"code {code} has no icon"


def test_the_icon_characters_are_declared_where_the_tests_expect(root):
    text = (root / "src" / "icons.cpp").read_text()
    codes = [int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]{2})", text)]
    assert len(codes) == len(bg.INPUTS)


def test_every_documented_setting_is_read_by_the_dll_and_the_reverse(root):
    read = set()
    for source in (root / "src").glob("*.cpp"):
        read |= {
            (section.lower(), key.lower())
            for section, key in re.findall(
                r'GetPrivateProfile(?:Int|String)A\(\s*"(\w+)",\s*"(\w+)"', source.read_text()
            )
        }
    ini = configparser.ConfigParser(interpolation=None)
    ini.read(root / "poppad.ini")
    documented = {(section.lower(), key.lower()) for section in ini.sections() for key in ini[section]}

    assert documented, "poppad.ini lists no settings"
    assert documented <= read, f"in poppad.ini but never read: {documented - read}"
    public = {pair for pair in read if pair[0] != "dev"}  # [Dev] is for the remote test rig only
    assert public <= documented, f"read by the DLL but not in poppad.ini: {public - documented}"


def test_the_shipped_ini_enables_icons_and_the_pad_fix(root):
    ini = configparser.ConfigParser(interpolation=None)
    ini.read(root / "poppad.ini")
    assert ini["Glyphs"]["Enabled"] == "1"
    assert ini["Glyphs"]["Style"] == "auto"
    assert ini["Input"]["NativeXInput"] == "1"
    assert "Dev" not in ini, "the shipped ini must not switch on the development rig"
