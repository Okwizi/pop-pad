// SPDX-License-Identifier: GPL-3.0-or-later
#include "font_file.h"
#include <cstring>

static const BYTE* find(const BYTE* data, DWORD size, const char* needle) {
    DWORD n = (DWORD)strlen(needle);
    for (DWORD i = 0; i + n <= size; i++)
        if (!memcmp(data + i, needle, n)) return data + i;
    return nullptr;
}

bool mft_parse(const BYTE* data, DWORD size, MftFont* font) {
    memset(font, 0, sizeof *font);
    if (size < 64 || memcmp(data, "Magma Font", 11) != 0) return false;
    lstrcpynA(font->name, (const char*)data + 15, sizeof font->name);

    const BYTE* ttf = find(data, size < 512 ? size : 512, ".ttf");
    if (!ttf) return false;
    const BYTE* p = ttf + 4;
    const BYTE* end = data + size;
    if (p + 12 > end) return false;
    font->pixel_height = p[1];
    p += 10;
    font->header = data;
    font->header_size = (DWORD)(p - data);

    memcpy(&font->glyph_count, p, 2);
    p += 2;
    if (font->glyph_count > MFT_MAX_GLYPHS || p + font->glyph_count * sizeof(MftGlyph) > end) return false;
    memcpy(font->glyphs, p, font->glyph_count * sizeof(MftGlyph));
    p += font->glyph_count * sizeof(MftGlyph);

    for (int block = 0; block < 256; block++) {
        if (p >= end) return false;
        font->block_present[block] = *p++ != 0;
        if (!font->block_present[block]) continue;
        if (p + 512 > end) return false;
        memcpy(font->blocks[block], p, 512);
        p += 512;
    }

    font->trailer = p;
    font->trailer_size = (DWORD)(end - p);
    if (font->trailer_size < 9) return false;
    font->page_count = p[8];
    if (font->page_count > MFT_MAX_PAGES || font->trailer_size < 9u + font->page_count * 4u) return false;
    for (int i = 0; i < font->page_count; i++) {
        memcpy(&font->page_width[i], p + 9 + i * 4, 2);
        memcpy(&font->page_height[i], p + 11 + i * 4, 2);
    }
    return true;
}

DWORD mft_write(const MftFont* font, BYTE* out, DWORD capacity) {
    DWORD blocks = 0;
    for (int i = 0; i < 256; i++) blocks += 1 + (font->block_present[i] ? 512 : 0);
    DWORD total = font->header_size + 2 + font->glyph_count * sizeof(MftGlyph) + blocks + font->trailer_size;
    if (total > capacity) return 0;

    BYTE* p = out;
    memcpy(p, font->header, font->header_size);
    p += font->header_size;
    memcpy(p, &font->glyph_count, 2);
    p += 2;
    memcpy(p, font->glyphs, font->glyph_count * sizeof(MftGlyph));
    p += font->glyph_count * sizeof(MftGlyph);
    for (int i = 0; i < 256; i++) {
        *p++ = font->block_present[i] ? 1 : 0;
        if (!font->block_present[i]) continue;
        memcpy(p, font->blocks[i], 512);
        p += 512;
    }
    memcpy(p, font->trailer, font->trailer_size);
    return total;
}
