// SPDX-License-Identifier: GPL-3.0-or-later
// Serves the game edited copies of two of its files, written to poppad\cache, so the originals
// stay untouched: the font tables gain one character per pad input, and the names of the pad
// inputs become those characters. See glyphs.cpp for the overall picture.
#include "common.h"
#include "font_file.h"
#include "icons.h"
#include <cstdio>
#include <cstring>

static bool write_cache(const char* name, const BYTE* data, DWORD size, char* path, size_t path_size) {
    char dir[MAX_PATH];
    snprintf(dir, sizeof dir, "%spoppad", g_dir);
    CreateDirectoryA(dir, nullptr);
    snprintf(dir, sizeof dir, "%spoppad\\cache", g_dir);
    CreateDirectoryA(dir, nullptr);
    snprintf(path, path_size, "%s\\%s", dir, name);
    HANDLE f = CreateFileA(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD written;
    bool ok = WriteFile(f, data, size, &written, nullptr) && written == size;
    CloseHandle(f);
    return ok;
}

static BYTE g_source[65536], g_output[65536 + ICON_TEST_COUNT * sizeof(MftGlyph)];
static MftFont g_font, g_patched;

static bool add_icon_characters(MftFont* font) {
    UINT width = font->page_width[0], height = font->page_height[0];
    UINT cell = width / ICON_COLUMNS;
    if (font->page_count == 0 || cell * ICON_COLUMNS != width || cell * ICON_ROWS != height) return false;
    if (font->glyph_count + ICON_TEST_COUNT > MFT_MAX_GLYPHS || !font->block_present[0]) return false;

    int cap = 0;
    for (int g = 0; g < font->glyph_count; g++)
        if (font->glyphs[g].code == 'X') cap = font->glyphs[g].height;
    if (cap == 0) return false;
    // A tile is three quarters button and one quarter margin, so this draws the button at one
    // and a half times the height of a capital letter, centred on it.
    int size = cap * 2;
    int top = (cap + size + 1) / 2;

    // Page 0 becomes twice as wide (see build_replacement), so everything already on it sits
    // at half its former horizontal position.
    for (int g = 0; g < font->glyph_count; g++) {
        MftGlyph& glyph = font->glyphs[g];
        if ((int)glyph.u0 != 0) continue;
        glyph.u0 *= 0.5f;
        glyph.u1 *= 0.5f;
    }
    bool scan = g_cfg.dev_icon_test >= 0x80;
    int count = scan ? (int)ICON_TEST_COUNT : (int)ICON_COUNT;
    for (int i = 0; i < count; i++) {
        BYTE code = scan ? (BYTE)(g_cfg.dev_icon_test + i) : ICON_CHARS[i];
        int tile = i % ICON_COUNT;
        MftGlyph& glyph = font->glyphs[font->glyph_count];
        glyph.code = code;
        glyph.width = glyph.height = (BYTE)size;
        glyph.left = 1;
        glyph.top = (signed char)top;
        glyph.advance = (BYTE)(size + 2);
        glyph.u0 = 0.5f + (float)(tile % ICON_COLUMNS) / (ICON_COLUMNS * 2);
        glyph.u1 = glyph.u0 + 1.0f / (ICON_COLUMNS * 2);
        glyph.v0 = (float)(tile / ICON_COLUMNS) / ICON_ROWS;
        glyph.v1 = glyph.v0 + 1.0f / ICON_ROWS;
        font->blocks[0][code] = font->glyph_count++;
    }
    return true;
}

bool font_redirect(const char* path, char* replaced, size_t replaced_size) {
    DWORD size = 0;
    if (!fs_read_whole(path, g_source, sizeof g_source, &size) || !mft_parse(g_source, size, &g_font)) {
        logf("font %s: not understood; left alone", path);
        return false;
    }
    g_patched = g_font;
    char name[96];
    snprintf(name, sizeof name, "%s.mft", g_font.name);
    DWORD out_size = add_icon_characters(&g_patched) ? mft_write(&g_patched, g_output, sizeof g_output) : 0;
    bool ok = out_size && write_cache(name, g_output, out_size, replaced, replaced_size);
    glyphs_font_opened(&g_font, ok);
    return ok;
}

// POPData.bf is a tiny archive: u32 directory size, then per file
//   u32 entry position, u32 data offset, u32 data size, u32 0, u32 name length, name
// followed by the file data. The names of every input live in pop4pclocal.dat, an ini-style
// text file, as "IOD_<device>_<code> = <name>" in three sections (normal, long, short).
static BYTE g_archive[262144], g_rebuilt[262144];

static DWORD get32(const BYTE* p) {
    DWORD v;
    memcpy(&v, p, 4);
    return v;
}
static void put32(BYTE* p, DWORD v) { memcpy(p, &v, 4); }

static DWORD replace_pad_names(const BYTE* text, DWORD size, BYTE* out) {
    DWORD n = 0;
    for (DWORD i = 0; i < size;) {
        DWORD end = i;
        while (end < size && text[end] != '\r' && text[end] != '\n') end++;
        DWORD p = i;
        while (p < end && (text[p] == ' ' || text[p] == '\t')) p++;
        int code = -1;
        if (end - p > 6 && !memcmp(text + p, "IOD_2_", 6)) {
            DWORD q = p + 6;
            code = 0;
            while (q < end && text[q] >= '0' && text[q] <= '9') code = code * 10 + (text[q++] - '0');
            while (q < end && (text[q] == ' ' || text[q] == '\t')) q++;
            if (q == p + 6 || q >= end || text[q] != '=' || code < FIRST_PAD_CODE || code >= FIRST_PAD_CODE + ICON_COUNT)
                code = -1;
            else
                p = q + 1;
        }
        BYTE name[16];
        int name_length = 0;
        if (code >= 0 && g_cfg.dev_icon_test) {
            // X, Y and B (codes 64, 65, 63) each show ten of the characters under test.
            int row = code == 64 ? 0 : code == 65 ? 1 : code == 63 ? 2 : -1;
            for (int k = 0; row >= 0 && k < 10; k++) {
                int index = row * 10 + k;
                if (g_cfg.dev_icon_test >= 0x80)
                    name[name_length++] = (BYTE)(g_cfg.dev_icon_test + index);
                else if (index < ICON_COUNT)
                    name[name_length++] = ICON_CHARS[index];
            }
        } else if (code >= 0) {
            name[name_length++] = ICON_CHARS[code - FIRST_PAD_CODE];
        }
        if (name_length) {
            memcpy(out + n, text + i, p - i);
            n += p - i;
            out[n++] = ' ';
            memcpy(out + n, name, name_length);
            n += name_length;
        } else {
            memcpy(out + n, text + i, end - i);
            n += end - i;
        }
        while (end < size && (text[end] == '\r' || text[end] == '\n')) out[n++] = text[end++];
        i = end;
    }
    return n;
}

bool names_redirect(const char* path, char* replaced, size_t replaced_size) {
    DWORD size = 0;
    if (!fs_read_whole(path, g_archive, sizeof g_archive / 2, &size) || size < 8) return false;
    DWORD directory = get32(g_archive);
    DWORD data_start = 4 + directory;
    if (data_start > size) return false;

    memcpy(g_rebuilt, g_archive, data_start);
    DWORD target = 0, target_offset = 0, target_size = 0;
    for (DWORD at = 4; at + 20 <= data_start;) {
        DWORD name_length = get32(g_archive + at + 16);
        if (at + 20 + name_length > data_start) return false;
        if (!lstrcmpiA((const char*)g_archive + at + 20, "pop4pclocal.dat")) {
            target = at;
            target_offset = get32(g_archive + at + 4);
            target_size = get32(g_archive + at + 8);
        }
        at += 20 + name_length;
    }
    if (!target || data_start + target_offset + target_size > size) {
        logf("names: %s has no pop4pclocal.dat; left alone", path);
        return false;
    }

    BYTE* out = g_rebuilt + data_start;
    memcpy(out, g_archive + data_start, target_offset);
    DWORD new_size = replace_pad_names(g_archive + data_start + target_offset, target_size, out + target_offset);
    DWORD rest_at = data_start + target_offset + target_size;
    memcpy(out + target_offset + new_size, g_archive + rest_at, size - rest_at);

    put32(g_rebuilt + target + 8, new_size);
    for (DWORD at = 4; at + 20 <= data_start;) {
        DWORD offset = get32(g_rebuilt + at + 4);
        if (offset > target_offset) put32(g_rebuilt + at + 4, offset + new_size - target_size);
        at += 20 + get32(g_rebuilt + at + 16);
    }

    bool ok = write_cache("POPData.bf", g_rebuilt, size + new_size - target_size, replaced, replaced_size);
    logf("names: pad input names %s", ok ? "replaced with icon characters" : "could NOT be replaced");
    return ok;
}
