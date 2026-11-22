// SPDX-License-Identifier: GPL-3.0-or-later
// Tests for the font table, the icon characters and the rewriting of the game's name file.
//
// The game's own files cannot be shipped, so every input here is built by the test, following
// the layouts written down in src/font_file.h and src/font_patch.cpp.
#include "common.h"
#include "font_file.h"
#include "icons.h"
#include "pad_style.h"

#include <cstdlib>
#include <initializer_list>
#include <string>
#include <vector>

Config g_cfg;
char g_dir[MAX_PATH];

struct OpenedFont {
    std::string name;
    bool patched;
};
static std::vector<OpenedFont> g_opened;

void logf(const char*, ...) {}

bool fs_read_whole(const char* path, BYTE* buffer, DWORD capacity, DWORD* size) {
    FILE* f = fopen(host_path(path).c_str(), "rb");
    if (!f) return false;
    *size = (DWORD)fread(buffer, 1, capacity, f);
    fclose(f);
    return *size < capacity;
}

void glyphs_font_opened(const MftFont* font, bool patched) { g_opened.push_back({font->name, patched}); }

// font_patch.cpp keeps add_icon_characters and replace_pad_names private; including it is the
// way to test them as they are.
#include "../../src/font_patch.cpp"

static int g_failures, g_checks;

#define CHECK(condition)                                                                                                                   \
    do {                                                                                                                                   \
        g_checks++;                                                                                                                        \
        if (!(condition)) {                                                                                                                \
            g_failures++;                                                                                                                  \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #condition);                                                  \
        }                                                                                                                                  \
    } while (0)

using Bytes = std::vector<BYTE>;

static void put8(Bytes& b, unsigned v) { b.push_back((BYTE)v); }
static void put16(Bytes& b, unsigned v) {
    put8(b, v & 0xFF);
    put8(b, v >> 8);
}
static void put32(Bytes& b, DWORD v) {
    put16(b, v & 0xFFFF);
    put16(b, v >> 16);
}
static void putf(Bytes& b, float v) {
    BYTE raw[4];
    memcpy(raw, &v, 4);
    b.insert(b.end(), raw, raw + 4);
}
static void put_text(Bytes& b, const char* text) { b.insert(b.end(), text, text + strlen(text)); }
static void pad_to(Bytes& b, size_t size) {
    while (b.size() < size) put8(b, 0);
}

// ---------------------------------------------------------------------------------- the font

enum { CAP_HEIGHT = 20, PAGE_WIDTH = 512, PAGE_HEIGHT = 256 };

struct SyntheticFont {
    Bytes data;
    size_t complete_size; // the shortest prefix that still parses
};

static void put_glyph(Bytes& b, unsigned code, unsigned height, float u0, float u1) {
    put16(b, code);
    put8(b, 12); // width
    put8(b, height);
    put8(b, 1);      // left
    put8(b, height); // top
    put8(b, 13);     // advance
    putf(b, u0);
    putf(b, 0.25f);
    putf(b, u1);
    putf(b, 0.5f);
}

// Three glyphs: 'X' and 'a' on page 0, 'b' on page 1; two pages of 512 x 256.
static SyntheticFont make_font() {
    Bytes b;
    put_text(b, "Magma Font");
    put8(b, 0);
    pad_to(b, 15);
    put_text(b, "TestFont");
    put8(b, 0);
    pad_to(b, 32);
    put_text(b, ".ttf");
    put8(b, 1);
    put8(b, 24); // pixel height
    pad_to(b, b.size() + 8);

    put16(b, 3);
    put_glyph(b, 'X', CAP_HEIGHT, 0.5f, 0.75f);
    put_glyph(b, 'a', 14, 0.125f, 0.25f);
    put_glyph(b, 'b', 14, 1.25f, 1.5f);

    for (int block = 0; block < 256; block++) {
        put8(b, block == 0);
        if (block != 0) continue;
        for (int code = 0; code < 256; code++) put16(b, code == 'X' ? 0 : code == 'a' ? 1 : code == 'b' ? 2 : 0);
    }

    putf(b, 1.0f);
    putf(b, 1.0f);
    put8(b, 2);
    for (int page = 0; page < 2; page++) {
        put16(b, PAGE_WIDTH);
        put16(b, PAGE_HEIGHT);
    }
    size_t complete = b.size();
    put_text(b, "TGA32");
    return {b, complete};
}

static MftFont g_parsed;

static void test_font_parses_the_fields_the_patcher_uses() {
    SyntheticFont f = make_font();
    CHECK(mft_parse(f.data.data(), (DWORD)f.data.size(), &g_parsed));
    CHECK(!strcmp(g_parsed.name, "TestFont"));
    CHECK(g_parsed.pixel_height == 24);
    CHECK(g_parsed.glyph_count == 3);
    CHECK(g_parsed.block_present[0] && !g_parsed.block_present[1]);
    CHECK(g_parsed.blocks[0]['a'] == 1 && g_parsed.blocks[0]['b'] == 2);
    CHECK(g_parsed.page_count == 2);
    CHECK(g_parsed.page_width[1] == PAGE_WIDTH && g_parsed.page_height[1] == PAGE_HEIGHT);
    CHECK(g_parsed.glyphs[0].code == 'X' && g_parsed.glyphs[0].height == CAP_HEIGHT);
}

static void test_font_write_reproduces_what_was_parsed() {
    SyntheticFont f = make_font();
    CHECK(mft_parse(f.data.data(), (DWORD)f.data.size(), &g_parsed));
    Bytes out(f.data.size() + 16);
    DWORD size = mft_write(&g_parsed, out.data(), (DWORD)out.size());
    CHECK(size == f.data.size());
    CHECK(!memcmp(out.data(), f.data.data(), f.data.size()));
    CHECK(mft_write(&g_parsed, out.data(), (DWORD)f.data.size() - 1) == 0);
}

static void test_font_rejects_input_cut_short() {
    SyntheticFont f = make_font();
    int accepted_too_early = 0;
    for (size_t length = 0; length < f.complete_size; length++) {
        // A buffer of exactly this length, so that reading past it is caught by the sanitizer.
        Bytes prefix(f.data.begin(), f.data.begin() + length);
        accepted_too_early += mft_parse(prefix.data(), (DWORD)length, &g_parsed);
    }
    CHECK(accepted_too_early == 0);
    CHECK(mft_parse(f.data.data(), (DWORD)f.complete_size, &g_parsed));
}

static void test_font_rejects_a_file_that_is_not_a_font() {
    SyntheticFont f = make_font();
    f.data[0] = 'm';
    CHECK(!mft_parse(f.data.data(), (DWORD)f.data.size(), &g_parsed));
}

// ------------------------------------------------------------------------------ the icon characters

static void test_icon_characters_avoid_the_games_placeholders() {
    // B3 B5 B6 B9 BA BB BC BD BE are what the game expands into the names of bound buttons.
    const BYTE placeholders[] = {0xB3, 0xB5, 0xB6, 0xB9, 0xBA, 0xBB, 0xBC, 0xBD, 0xBE};
    bool seen[256] = {};
    for (BYTE icon : ICON_CHARS) {
        CHECK(icon >= 0xA0);
        CHECK(!seen[icon]);
        seen[icon] = true;
        for (BYTE placeholder : placeholders) CHECK(icon != placeholder);
    }
    CHECK(sizeof ICON_CHARS == ICON_COUNT);
}

static void test_icons_are_added_to_the_first_page() {
    g_cfg = {};
    SyntheticFont f = make_font();
    static MftFont font;
    CHECK(mft_parse(f.data.data(), (DWORD)f.data.size(), &font));
    CHECK(add_icon_characters(&font));
    CHECK(font.glyph_count == 3 + ICON_COUNT);

    // The first page doubles in width, so what was already on it moves to the left half.
    CHECK(font.glyphs[0].u0 == 0.25f && font.glyphs[0].u1 == 0.375f);
    CHECK(font.glyphs[1].u0 == 0.0625f);
    CHECK(font.glyphs[2].u0 == 1.25f); // page 1 is untouched

    const int size = CAP_HEIGHT * 2;
    for (int i = 0; i < ICON_COUNT; i++) {
        const MftGlyph& icon = font.glyphs[font.blocks[0][ICON_CHARS[i]]];
        CHECK(font.blocks[0][ICON_CHARS[i]] == 3 + i);
        CHECK(icon.code == ICON_CHARS[i]);
        CHECK(icon.width == size && icon.height == size);
        CHECK(icon.top == (CAP_HEIGHT + size + 1) / 2);
        CHECK(icon.u0 >= 0.5f && icon.u1 <= 1.0f);
        CHECK(icon.v1 - icon.v0 == 1.0f / ICON_ROWS);
    }
    // The atlas has 8 columns, so tile 9 is the second tile of the second row.
    const MftGlyph& tile9 = font.glyphs[font.blocks[0][ICON_CHARS[9]]];
    CHECK(tile9.u0 == 0.5f + 1.0f / (ICON_COLUMNS * 2) && tile9.v0 == 1.0f / ICON_ROWS);
}

static void test_icons_are_refused_when_the_page_cannot_carry_them() {
    g_cfg = {};
    SyntheticFont f = make_font();
    static MftFont font;

    CHECK(mft_parse(f.data.data(), (DWORD)f.data.size(), &font));
    font.page_width[0] = 500; // not a whole number of icon columns
    CHECK(!add_icon_characters(&font));

    CHECK(mft_parse(f.data.data(), (DWORD)f.data.size(), &font));
    font.glyphs[0].code = 'Y'; // no capital X to size the icons by
    CHECK(!add_icon_characters(&font));

    CHECK(mft_parse(f.data.data(), (DWORD)f.data.size(), &font));
    font.glyph_count = MFT_MAX_GLYPHS - 1; // no room left in the table
    CHECK(!add_icon_characters(&font));
}

// ------------------------------------------------------------------------------------ the names

static std::string rewrite(const std::string& text) {
    static BYTE out[4096];
    DWORD n = replace_pad_names((const BYTE*)text.data(), (DWORD)text.size(), out);
    return std::string((const char*)out, n);
}

static std::string icon(int code) { return std::string(1, (char)ICON_CHARS[code - FIRST_PAD_CODE]); }

static void test_pad_names_become_icon_characters() {
    g_cfg = {};
    CHECK(rewrite("IOD_2_62 = A\r\n") == "IOD_2_62 = " + icon(62) + "\r\n");
    CHECK(rewrite("IOD_2_63=B\r\n") == "IOD_2_63= " + icon(63) + "\r\n");
    CHECK(rewrite("  IOD_2_52 = Up\n") == "  IOD_2_52 = " + icon(52) + "\n");
    CHECK(rewrite("IOD_2_75 = Right stick\r\nIOD_2_52 = Up") == "IOD_2_75 = " + icon(75) + "\r\nIOD_2_52 = " + icon(52));
}

static void test_other_lines_are_copied_unchanged() {
    g_cfg = {};
    const std::string untouched = "[KeysLong]\r\n"
                                  "IOD_1_30 = Return\r\n"           // keyboard
                                  "IOD_1_62 = Same number as A\r\n" // keyboard, with a pad input's number
                                  "IOD_0_63 = Same number as B\r\n" // another device class
                                  "IOD_2_51 = Below the pad\r\n"    // one before the first pad input
                                  "IOD_2_76 = Above the pad\r\n"    // one after the last
                                  "IOD_2_ = no code\r\n"
                                  "IOD_2_62 no equals sign\r\n"
                                  "IOD_2\r\n"
                                  "\r\n"
                                  "text with IOD_2_62 = A inside\r\n";
    CHECK(rewrite(untouched) == untouched);
}

// ------------------------------------------------------------------------------------- the archive

struct ArchiveFile {
    std::string name;
    Bytes data;
};

// POPData.bf: u32 directory size, then per file {position, data offset, data size, 0, name
// length, name with its NUL}, then the data back to back.
static Bytes make_archive(const std::vector<ArchiveFile>& files) {
    Bytes directory, data;
    for (const ArchiveFile& file : files) {
        put32(directory, (DWORD)directory.size());
        put32(directory, (DWORD)data.size());
        put32(directory, (DWORD)file.data.size());
        put32(directory, 0);
        put32(directory, (DWORD)file.name.size() + 1);
        put_text(directory, file.name.c_str());
        put8(directory, 0);
        data.insert(data.end(), file.data.begin(), file.data.end());
    }
    Bytes archive;
    put32(archive, (DWORD)directory.size());
    archive.insert(archive.end(), directory.begin(), directory.end());
    archive.insert(archive.end(), data.begin(), data.end());
    return archive;
}

static std::vector<ArchiveFile> read_archive(const Bytes& archive) {
    std::vector<ArchiveFile> files;
    DWORD directory = get32(archive.data());
    DWORD data_start = 4 + directory;
    for (DWORD at = 4; at + 20 <= data_start;) {
        DWORD offset = get32(&archive[at + 4]), size = get32(&archive[at + 8]), name_length = get32(&archive[at + 16]);
        ArchiveFile file;
        file.name = (const char*)&archive[at + 20];
        file.data.assign(archive.begin() + data_start + offset, archive.begin() + data_start + offset + size);
        files.push_back(file);
        at += 20 + name_length;
    }
    return files;
}

static std::string g_workdir;

static std::string work_path(const char* name) { return g_workdir + name; }

static void write_work_file(const char* name, const Bytes& data) {
    FILE* f = fopen(work_path(name).c_str(), "wb");
    fwrite(data.data(), 1, data.size(), f);
    fclose(f);
}

static Bytes read_file(const std::string& path) {
    Bytes data;
    FILE* f = fopen(host_path(path.c_str()).c_str(), "rb");
    if (!f) return data;
    BYTE chunk[4096];
    for (size_t n; (n = fread(chunk, 1, sizeof chunk, f)) > 0;) data.insert(data.end(), chunk, chunk + n);
    fclose(f);
    return data;
}

static Bytes bytes_of(const std::string& text) { return Bytes(text.begin(), text.end()); }

// The name file goes in the middle of the archive with a file on each side, so a wrong offset
// for the file after it shows up. `names` and `expected` differ in length, which is the case
// that needs the offsets to move.
static void check_names_rewritten_in_archive(const std::string& names, const std::string& expected) {
    g_cfg = {};
    const Bytes before = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    const Bytes after = {11, 12, 13, 14, 15, 16, 17};
    write_work_file("POPData.bf", make_archive({{"first.bin", before}, {"pop4pclocal.dat", bytes_of(names)}, {"last.bin", after}}));

    char replaced[MAX_PATH];
    CHECK(names_redirect(work_path("POPData.bf").c_str(), replaced, sizeof replaced));
    CHECK(strstr(replaced, "POPData.bf") != nullptr);
    CHECK(expected.size() != names.size());

    Bytes rewritten = read_file(replaced);
    std::vector<ArchiveFile> files = read_archive(rewritten);
    CHECK(files.size() == 3);
    if (files.size() != 3) return;
    CHECK(files[0].name == "first.bin" && files[0].data == before);
    CHECK(files[1].name == "pop4pclocal.dat" && files[1].data == bytes_of(expected));
    CHECK(files[2].name == "last.bin" && files[2].data == after);
    CHECK(rewritten.size() == 4 + 3 * 20 + 9 + 1 + 15 + 1 + 8 + 1 + before.size() + expected.size() + after.size());
}

static void test_the_name_file_is_rewritten_inside_the_archive() {
    // Longer names than an icon: the file gets smaller and what follows moves up.
    check_names_rewritten_in_archive("[Keys]\r\nIOD_2_62 = Button A\r\nIOD_1_30 = Return\r\nIOD_2_64 = Button X\r\n",
                                     "[Keys]\r\nIOD_2_62 = " + icon(62) + "\r\nIOD_1_30 = Return\r\nIOD_2_64 = " + icon(64) + "\r\n");
    // No name at all: the file gets bigger and what follows moves down.
    check_names_rewritten_in_archive("[Keys]\r\nIOD_2_62 =\r\nIOD_2_63 =\r\n",
                                     "[Keys]\r\nIOD_2_62 = " + icon(62) + "\r\nIOD_2_63 = " + icon(63) + "\r\n");
}

static void test_an_archive_without_the_name_file_is_left_alone() {
    g_cfg = {};
    write_work_file("Other.bf", make_archive({{"first.bin", {1, 2, 3}}, {"last.bin", {4, 5}}}));
    char replaced[MAX_PATH] = "unchanged";
    CHECK(!names_redirect(work_path("Other.bf").c_str(), replaced, sizeof replaced));
    CHECK(!strcmp(replaced, "unchanged"));

    write_work_file("Tiny.bf", {1, 2, 3});
    CHECK(!names_redirect(work_path("Tiny.bf").c_str(), replaced, sizeof replaced));
}

static void test_a_font_file_is_served_with_the_icons_added() {
    g_cfg = {};
    g_opened.clear();
    SyntheticFont f = make_font();
    write_work_file("source.mft", f.data);

    char replaced[MAX_PATH];
    CHECK(font_redirect(work_path("source.mft").c_str(), replaced, sizeof replaced));
    CHECK(strstr(replaced, "TestFont.mft") != nullptr);
    CHECK(g_opened.size() == 1 && g_opened[0].name == "TestFont" && g_opened[0].patched);

    Bytes written = read_file(replaced);
    static MftFont font;
    CHECK(mft_parse(written.data(), (DWORD)written.size(), &font));
    CHECK(font.glyph_count == 3 + ICON_COUNT);
    CHECK(font.blocks[0][ICON_CHARS[0]] == 3);
}

static void test_a_file_that_is_not_a_font_is_not_served() {
    g_cfg = {};
    g_opened.clear();
    write_work_file("notafont.mft", bytes_of("this is not a font table, whatever the extension says, so it must be left alone"));
    char replaced[MAX_PATH];
    CHECK(!font_redirect(work_path("notafont.mft").c_str(), replaced, sizeof replaced));
    CHECK(g_opened.empty());
}

static bool ids_of(const char* path, unsigned want_vendor, unsigned want_product) {
    unsigned vendor = 0, product = 0;
    return hid_path_ids(path, &vendor, &product) && vendor == want_vendor && product == want_product;
}

static void test_device_paths_give_their_vendor_and_product() {
    CHECK(ids_of("\\\\?\\hid#vid_2dc8&pid_310a&mi_00#7&1a2b3c4d&0&0000#{4d1e55b2-f16f-11cf-88cb-001111000030}", 0x2DC8, 0x310A));
    CHECK(ids_of("\\\\?\\usb#VID_054C&PID_0CE6#5&2f3e4d&0&3#{ec87f1e3-c13b-4100-b5f7-8b84d54260cb}", 0x054C, 0x0CE6));
    CHECK(ids_of("vid_045e&pid_028e", 0x045E, 0x028E));

    unsigned vendor = 0, product = 0;
    CHECK(!hid_path_ids("", &vendor, &product));
    CHECK(!hid_path_ids("\\\\?\\root#system#0001#{guid}", &vendor, &product));
    CHECK(!hid_path_ids("vid_2dc", &vendor, &product));
    CHECK(!hid_path_ids("vid_2dcg&pid_310a", &vendor, &product));
    CHECK(!hid_path_ids("vid_2dc8_pid_310a", &vendor, &product));
    CHECK(!hid_path_ids("vid_2dc8&pid_31", &vendor, &product));
    CHECK(!hid_path_ids("vid_2dc8&pid_", &vendor, &product));
    // A first "vid_" that is not followed by a pad id must not hide a later one that is.
    CHECK(ids_of("x#vid_zzzz#vid_057e&pid_2009#", 0x057E, 0x2009));
}

static const char* style_for(std::initializer_list<unsigned> vendors) {
    PadVendors set = {};
    for (unsigned vendor : vendors) pad_vendors_add(&set, vendor);
    return pad_style_for(set);
}

static void test_the_icon_set_follows_the_makers_that_are_present() {
    CHECK(!strcmp(style_for({}), "xbox"));
    CHECK(!strcmp(style_for({0x045E}), "xbox"));
    CHECK(!strcmp(style_for({0x054C}), "playstation"));
    CHECK(!strcmp(style_for({0x057E}), "switch"));
    CHECK(!strcmp(style_for({0x2DC8}), "generic"));
    // With several pads plugged in, the order is PlayStation, Switch, other makers, Xbox.
    CHECK(!strcmp(style_for({0x045E, 0x054C}), "playstation"));
    CHECK(!strcmp(style_for({0x057E, 0x054C}), "playstation"));
    CHECK(!strcmp(style_for({0x2DC8, 0x057E}), "switch"));
    CHECK(!strcmp(style_for({0x045E, 0x2DC8}), "generic"));
}

static void test_steam_inputs_virtual_pad_does_not_decide_the_icon_set() {
    const unsigned valve = 0x28DE;
    CHECK(!strcmp(style_for({valve}), "xbox"));
    CHECK(!strcmp(style_for({valve, 0x054C}), "playstation"));
    CHECK(!strcmp(style_for({valve, 0x2DC8}), "generic"));
}

int main() {
    char templ[] = "/tmp/poppad-native-XXXXXX";
    if (!mkdtemp(templ)) {
        fprintf(stderr, "cannot create a work folder\n");
        return 2;
    }
    g_workdir = std::string(templ) + "/";
    snprintf(g_dir, sizeof g_dir, "%s", g_workdir.c_str());

    test_font_parses_the_fields_the_patcher_uses();
    test_font_write_reproduces_what_was_parsed();
    test_font_rejects_input_cut_short();
    test_font_rejects_a_file_that_is_not_a_font();
    test_icon_characters_avoid_the_games_placeholders();
    test_icons_are_added_to_the_first_page();
    test_icons_are_refused_when_the_page_cannot_carry_them();
    test_pad_names_become_icon_characters();
    test_other_lines_are_copied_unchanged();
    test_the_name_file_is_rewritten_inside_the_archive();
    test_an_archive_without_the_name_file_is_left_alone();
    test_a_font_file_is_served_with_the_icons_added();
    test_a_file_that_is_not_a_font_is_not_served();
    test_device_paths_give_their_vendor_and_product();
    test_the_icon_set_follows_the_makers_that_are_present();
    test_steam_inputs_virtual_pad_does_not_decide_the_icon_set();

    std::string cleanup = "rm -rf '" + std::string(templ) + "'";
    if (system(cleanup.c_str()) != 0) fprintf(stderr, "could not remove %s\n", templ);
    printf("%d checks, %d failed\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
