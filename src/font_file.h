// SPDX-License-Identifier: GPL-3.0-or-later
// The menu font description ("Magma Font", .mft), as far as pop-pad needs to understand it.
//
//   opaque header, ending with the source .ttf name
//   u8 1, u8 pixel height, 8 zero bytes
//   u16 glyph count, then 23-byte glyph records
//   256 x { u8 present; if present: 256 x u16 }   character code -> glyph record number,
//                                                 one block per high byte of the code
//   2 floats, u8 page count, page count x { u16 width, u16 height }, "TGA32"
//
// A glyph's four texture coordinates carry the atlas page in their integer part and the
// position on that page, as a fraction of the page size, in their fractional part.
#pragma once
#include "common.h"

#pragma pack(push, 1)
struct MftGlyph {
    WORD code;
    BYTE width, height;
    signed char left, top; // pixels from the pen to the left edge, and from the baseline up to the top
    BYTE advance;
    float u0, v0, u1, v1;
};
#pragma pack(pop)

enum { MFT_MAX_GLYPHS = 512, MFT_MAX_PAGES = 8 };

struct MftFont {
    char name[64];
    BYTE pixel_height;
    const BYTE* header; // points into the source buffer
    DWORD header_size;  // everything before the glyph count
    WORD glyph_count;
    MftGlyph glyphs[MFT_MAX_GLYPHS];
    bool block_present[256];
    WORD blocks[256][256];
    const BYTE* trailer; // points into the source buffer
    DWORD trailer_size;
    BYTE page_count;
    WORD page_width[MFT_MAX_PAGES], page_height[MFT_MAX_PAGES];
};

bool mft_parse(const BYTE* data, DWORD size, MftFont* font);
DWORD mft_write(const MftFont* font, BYTE* out, DWORD capacity);
