// SPDX-License-Identifier: GPL-3.0-or-later
// Layout shared by the pieces that put button icons into the game's text (see glyphs.cpp).
#pragma once
#include "common.h"

// The pad inputs the game knows, in its own numbering: 52 is D-pad up ... 75 is right stick right.
enum { FIRST_PAD_CODE = 52, ICON_COUNT = 24 };

// An icon atlas is ICON_COLUMNS x ICON_ROWS tiles of ICON_TILE pixels, one tile per pad input
// in the order above, read from poppad\glyphs\<style>.tga.
enum { ICON_COLUMNS = 8, ICON_ROWS = 4, ICON_TILE = 128 };
enum { ATLAS_WIDTH = ICON_COLUMNS * ICON_TILE, ATLAS_HEIGHT = ICON_ROWS * ICON_TILE };

// The character that stands for each pad input once its name has been replaced. They are bytes
// in 0xA0-0xFF, where the Western code page and plain zero-extension give the same character,
// and none is a character the game's own text uses.
//
// Nine characters must never appear here: B3, B5, B6, B9, BA, BB, BC, BD and BE. The game
// uses them (and their box-drawing twins U+2524..U+255B) as placeholders for controller
// buttons and replaces each with the name of whatever is bound, so an icon given one of them
// is expanded a second time and comes out as a placeholder box.
extern const BYTE ICON_CHARS[ICON_COUNT];

// Development aid, [Dev] IconTest: the names of X, Y and B each show ten characters, so a
// whole set can be checked in one screenshot of the Combo List.
//   1          the 24 characters above, in order
//   80..FF     (hex) thirty consecutive byte values from there, each given tile = position
//              modulo ICON_COUNT, to find out which characters the game leaves alone
enum { ICON_TEST_COUNT = 30 };

bool glyphs_init();
void brackets_install();
