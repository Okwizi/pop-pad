// SPDX-License-Identifier: GPL-3.0-or-later
// Button icons in the game's own text.
//
// The menu fonts are small texture atlases plus a table saying where each character sits. To
// show an icon wherever the game prints the name of a pad button, three things are changed,
// none of them in the game's own files:
//   - the font table gains one character per pad input, pointing at an icon tile   (font_redirect)
//   - the names of the pad inputs become those single characters                   (names_redirect)
//   - page 0 of each font is swapped, when the game binds it, for a larger texture holding the
//     original page in its left half and the icon atlas in its right half          (this file)
#include "common.h"
#include "font_file.h"
#include "icons.h"
#include <d3d9.h>
#include <cstdio>
#include <cstring>

enum { MAX_FONTS = 4, MAX_LIVE = 16 };
static const D3DFORMAT FORMAT_DXT5 = (D3DFORMAT)MAKEFOURCC('D', 'X', 'T', '5');

struct Font {
    MftFont table;                  // as shipped, used to recognise its pages
    bool patched;                   // the game was given the table with icon characters
    IDirect3DTexture9* replacement; // managed pool, so it survives display-mode changes
};
static Font g_fonts[MAX_FONTS];
static int g_font_count;

// The game keeps its textures in the default pool, so a page is destroyed and uploaded again
// whenever the display mode changes. Nothing here may hold a reference to one of those (that
// would make the mode change fail); a page is recognised afresh each time it is uploaded.
struct LivePage {
    IDirect3DBaseTexture9* texture;
    IDirect3DTexture9* replacement;
};
static LivePage g_live[MAX_LIVE];
static int g_live_count;

static BYTE* g_atlas; // ATLAS_WIDTH x ATLAS_HEIGHT BGRA, top row first

static const char* detect_style() {
    UINT count = 0;
    if (GetRawInputDeviceList(nullptr, &count, sizeof(RAWINPUTDEVICELIST)) != 0 || count == 0) return "xbox";
    RAWINPUTDEVICELIST* list = (RAWINPUTDEVICELIST*)HeapAlloc(GetProcessHeap(), 0, count * sizeof(RAWINPUTDEVICELIST));
    if (!list) return "xbox";
    bool playstation = false, nintendo = false, xbox = false, other = false;
    UINT got = GetRawInputDeviceList(list, &count, sizeof(RAWINPUTDEVICELIST));
    for (UINT i = 0; got != (UINT)-1 && i < got; i++) {
        if (list[i].dwType != RIM_TYPEHID) continue;
        RID_DEVICE_INFO info;
        info.cbSize = sizeof info;
        UINT size = sizeof info;
        if ((int)GetRawInputDeviceInfoA(list[i].hDevice, RIDI_DEVICEINFO, &info, &size) <= 0) continue;
        if (info.hid.usUsagePage != 1 || (info.hid.usUsage != 4 && info.hid.usUsage != 5)) continue;
        logf("controller present: %04lX:%04lX", info.hid.dwVendorId, info.hid.dwProductId);
        switch (info.hid.dwVendorId) {
            case 0x054C: playstation = true; break;
            case 0x057E: nintendo = true; break;
            case 0x045E: xbox = true; break;
            default: other = true; break;
        }
    }
    HeapFree(GetProcessHeap(), 0, list);
    // Steam Input adds a virtual Xbox pad beside the real controller, so the real one decides.
    if (playstation) return "playstation";
    if (nintendo) return "switch";
    if (other) return "generic";
    (void)xbox;
    return "xbox";
}

bool glyphs_init() {
    char style[32], ini[MAX_PATH], path[MAX_PATH];
    snprintf(ini, sizeof ini, "%spoppad.ini", g_dir);
    GetPrivateProfileStringA("Glyphs", "Style", "auto", style, sizeof style, ini);
    if (!lstrcmpiA(style, "auto")) lstrcpynA(style, detect_style(), sizeof style);
    snprintf(path, sizeof path, "%spoppad\\glyphs\\%s.tga", g_dir, style);

    const DWORD pixels = ATLAS_WIDTH * ATLAS_HEIGHT * 4;
    BYTE* file = (BYTE*)HeapAlloc(GetProcessHeap(), 0, pixels + 64);
    DWORD size = 0;
    bool ok = file && fs_read_whole(path, file, pixels + 64, &size) && size == 18 + pixels && file[2] == 2 && file[16] == 32 &&
              (file[17] & 0x20) && (file[12] | file[13] << 8) == ATLAS_WIDTH && (file[14] | file[15] << 8) == ATLAS_HEIGHT;
    if (!ok) {
        logf("icons: %s is missing or not a %dx%d 32-bit TGA; icons disabled", path, ATLAS_WIDTH, ATLAS_HEIGHT);
        if (file) HeapFree(GetProcessHeap(), 0, file);
        return false;
    }
    g_atlas = file + 18;
    logf("icons: style %s", style);
    return true;
}

void glyphs_font_opened(const MftFont* font, bool patched) {
    for (int i = 0; i < g_font_count; i++) {
        if (lstrcmpiA(g_fonts[i].table.name, font->name)) continue;
        g_fonts[i].patched = patched;
        return;
    }
    if (g_font_count >= MAX_FONTS) return;
    g_fonts[g_font_count].table = *font;
    g_fonts[g_font_count].patched = patched;
    g_fonts[g_font_count].replacement = nullptr;
    g_font_count++;
    logf("font %s: %d glyphs, %d pages, icons %s", font->name, font->glyph_count, font->page_count, patched ? "added" : "NOT added");
}

static void decode_dxt5(const BYTE* src, int pitch, UINT width, UINT height, BYTE* out) {
    for (UINT by = 0; by < height; by += 4) {
        const BYTE* block = src + (by / 4) * pitch;
        for (UINT bx = 0; bx < width; bx += 4, block += 16) {
            BYTE alpha[8];
            alpha[0] = block[0];
            alpha[1] = block[1];
            if (alpha[0] > alpha[1]) {
                for (int i = 1; i < 7; i++) alpha[i + 1] = (BYTE)(((7 - i) * alpha[0] + i * alpha[1]) / 7);
            } else {
                for (int i = 1; i < 5; i++) alpha[i + 1] = (BYTE)(((5 - i) * alpha[0] + i * alpha[1]) / 5);
                alpha[6] = 0;
                alpha[7] = 255;
            }
            unsigned long long alpha_bits = 0;
            for (int i = 0; i < 6; i++) alpha_bits |= (unsigned long long)block[2 + i] << (8 * i);

            WORD ends[2];
            memcpy(ends, block + 8, 4);
            BYTE color[4][3];
            for (int i = 0; i < 2; i++) {
                color[i][2] = (BYTE)(((ends[i] >> 11) & 31) * 255 / 31);
                color[i][1] = (BYTE)(((ends[i] >> 5) & 63) * 255 / 63);
                color[i][0] = (BYTE)((ends[i] & 31) * 255 / 31);
            }
            for (int ch = 0; ch < 3; ch++) {
                color[2][ch] = (BYTE)((2 * color[0][ch] + color[1][ch]) / 3);
                color[3][ch] = (BYTE)((color[0][ch] + 2 * color[1][ch]) / 3);
            }
            DWORD color_bits;
            memcpy(&color_bits, block + 12, 4);

            for (int y = 0; y < 4; y++) {
                for (int x = 0; x < 4; x++) {
                    if (bx + x >= width || by + y >= height) continue;
                    int n = y * 4 + x;
                    BYTE* p = out + ((by + y) * width + bx + x) * 4;
                    const BYTE* c = color[(color_bits >> (2 * n)) & 3];
                    p[0] = c[0];
                    p[1] = c[1];
                    p[2] = c[2];
                    p[3] = alpha[(alpha_bits >> (3 * n)) & 7];
                }
            }
        }
    }
}

static bool inked(const BYTE* pixels, UINT width, UINT height, int x, int y) {
    return x >= 0 && y >= 0 && (UINT)x < width && (UINT)y < height && pixels[(y * width + x) * 4 + 3] > 48;
}

// How well an image matches one page of a font: the share of that page's glyphs whose cell
// holds ink while the one-pixel frame around it is clear. Only the real page lines up with
// every cell. The font table measures down from the top of the artwork, while the texture
// holds the artwork bottom row first, hence the flipped row.
static float page_score(const MftFont& font, int page, const BYTE* pixels, UINT width, UINT height) {
    int cells = 0, clean = 0;
    for (int g = 0; g < font.glyph_count; g++) {
        const MftGlyph& glyph = font.glyphs[g];
        if ((int)glyph.u0 != page || glyph.width < 2 || glyph.height < 2) continue;
        int x0 = (int)((glyph.u0 - page) * width + 0.5f);
        int top = (int)((glyph.v0 - page) * height + 0.5f);
        int y0 = (int)height - top - glyph.height;
        int x1 = x0 + glyph.width, y1 = y0 + glyph.height;
        cells++;
        bool inside = false, frame = false;
        for (int y = y0; y < y1 && !inside; y++)
            for (int x = x0; x < x1 && !inside; x++) inside = inked(pixels, width, height, x, y);
        for (int x = x0 - 1; x <= x1 && !frame; x++) frame = inked(pixels, width, height, x, y0 - 1) || inked(pixels, width, height, x, y1);
        for (int y = y0 - 1; y <= y1 && !frame; y++) frame = inked(pixels, width, height, x0 - 1, y) || inked(pixels, width, height, x1, y);
        clean += inside && !frame;
    }
    return cells ? (float)clean / cells : 0;
}

static void write_tga(const char* name, const BYTE* pixels, UINT width, UINT height) {
    char path[MAX_PATH];
    snprintf(path, sizeof path, "%spoppad_shots", g_dir);
    CreateDirectoryA(path, nullptr);
    snprintf(path, sizeof path, "%spoppad_shots\\%s.tga", g_dir, name);
    HANDLE f = CreateFileA(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    BYTE header[18] = {};
    header[2] = 2;
    header[12] = (BYTE)width;
    header[13] = (BYTE)(width >> 8);
    header[14] = (BYTE)height;
    header[15] = (BYTE)(height >> 8);
    header[16] = 32;
    header[17] = 0x28;
    DWORD written;
    WriteFile(f, header, sizeof header, &written, nullptr);
    WriteFile(f, pixels, width * height * 4, &written, nullptr);
    CloseHandle(f);
}

// The replacement keeps the page's proportions on its left half, so the font table only has to
// halve the horizontal coordinate of the existing characters, and is large enough that an icon
// tile keeps its full resolution.
static IDirect3DTexture9* build_replacement(IDirect3DDevice9* device, const BYTE* page, UINT width, UINT height) {
    UINT scale = ATLAS_WIDTH / width;
    if (scale == 0 || width * scale != ATLAS_WIDTH || height * scale != ATLAS_HEIGHT) {
        logf("icons: a %ux%u page cannot carry the icon atlas", width, height);
        return nullptr;
    }
    IDirect3DTexture9* texture = nullptr;
    D3DLOCKED_RECT lock;
    if (FAILED(device->CreateTexture(ATLAS_WIDTH * 2, ATLAS_HEIGHT, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &texture, nullptr)))
        return nullptr;
    if (FAILED(texture->LockRect(0, &lock, nullptr, 0))) {
        texture->Release();
        return nullptr;
    }
    for (UINT y = 0; y < ATLAS_HEIGHT; y++) {
        BYTE* row = (BYTE*)lock.pBits + y * lock.Pitch;

        float fy = (y + 0.5f) / scale - 0.5f;
        int y0 = fy < 0 ? 0 : (int)fy;
        int y1 = y0 + 1 < (int)height ? y0 + 1 : y0;
        float wy = fy < 0 ? 0 : fy - y0;
        for (UINT x = 0; x < ATLAS_WIDTH; x++) {
            float fx = (x + 0.5f) / scale - 0.5f;
            int x0 = fx < 0 ? 0 : (int)fx;
            int x1 = x0 + 1 < (int)width ? x0 + 1 : x0;
            float wx = fx < 0 ? 0 : fx - x0;
            const BYTE* p00 = page + (y0 * width + x0) * 4;
            const BYTE* p01 = page + (y0 * width + x1) * 4;
            const BYTE* p10 = page + (y1 * width + x0) * 4;
            const BYTE* p11 = page + (y1 * width + x1) * 4;
            for (int ch = 0; ch < 4; ch++) {
                float top = p00[ch] + (p01[ch] - p00[ch]) * wx;
                float bottom = p10[ch] + (p11[ch] - p10[ch]) * wx;
                row[x * 4 + ch] = (BYTE)(top + (bottom - top) * wy + 0.5f);
            }
        }

        // The game's pages are stored bottom row first; the icon atlas file is top row first.
        memcpy(row + ATLAS_WIDTH * 4, g_atlas + (ATLAS_HEIGHT - 1 - y) * ATLAS_WIDTH * 4, ATLAS_WIDTH * 4);
    }
    texture->UnlockRect(0);
    return texture;
}

static void forget(IDirect3DBaseTexture9* texture) {
    for (int i = 0; i < g_live_count; i++) {
        if (g_live[i].texture != texture) continue;
        g_live[i] = g_live[--g_live_count];
        return;
    }
}

// A page must be forgotten the moment the game lets go of it: Direct3D reuses the address for
// the next texture it creates, which would then be swapped for the icon page by mistake.
typedef ULONG(STDMETHODCALLTYPE* Release_t)(IUnknown*);
struct ReleaseHook {
    void** vtable;
    Release_t original;
};
static ReleaseHook g_release_hooks[4];
static int g_release_hook_count;

static ULONG STDMETHODCALLTYPE hk_texture_release(IUnknown* self) {
    void** vtable = *(void***)self;
    Release_t original = nullptr;
    for (int i = 0; i < g_release_hook_count; i++)
        if (g_release_hooks[i].vtable == vtable) original = g_release_hooks[i].original;
    ULONG remaining = original(self);
    if (remaining == 0) forget((IDirect3DBaseTexture9*)self);
    return remaining;
}

static bool watch_release(IDirect3DBaseTexture9* texture) {
    void** vtable = *(void***)texture;
    for (int i = 0; i < g_release_hook_count; i++)
        if (g_release_hooks[i].vtable == vtable) return true;
    if (g_release_hook_count >= 4) return false;
    void* previous = vt_patch(texture, 2, (void*)hk_texture_release);
    if (!previous) return false;
    g_release_hooks[g_release_hook_count++] = {vtable, (Release_t)previous};
    return true;
}

void glyphs_device_reset() { g_live_count = 0; }

// Called when the game uploads a texture: source is the system-memory copy it just filled.
void glyphs_texture_uploaded(IDirect3DDevice9* device, IDirect3DBaseTexture9* source, IDirect3DBaseTexture9* destination) {
    forget(destination);
    if (g_font_count == 0 || source->GetType() != D3DRTYPE_TEXTURE) return;
    IDirect3DTexture9* texture = (IDirect3DTexture9*)source;
    D3DSURFACE_DESC desc;
    if (FAILED(texture->GetLevelDesc(0, &desc)) || desc.Format != FORMAT_DXT5) return;

    bool candidate = false;
    for (int f = 0; f < g_font_count; f++) {
        for (int p = 0; p < g_fonts[f].table.page_count; p++) {
            candidate |= g_fonts[f].table.page_width[p] == desc.Width && g_fonts[f].table.page_height[p] == desc.Height;
        }
    }
    static BYTE pixels[512 * 256 * 4];
    if (!candidate || desc.Width * desc.Height * 4 > sizeof pixels) return;

    D3DLOCKED_RECT lock;
    if (FAILED(texture->LockRect(0, &lock, nullptr, D3DLOCK_READONLY))) return;
    decode_dxt5((const BYTE*)lock.pBits, lock.Pitch, desc.Width, desc.Height, pixels);
    texture->UnlockRect(0);

    int best_font = -1, best_page = -1;
    float best = 0;
    for (int f = 0; f < g_font_count; f++) {
        for (int p = 0; p < g_fonts[f].table.page_count; p++) {
            if (g_fonts[f].table.page_width[p] != desc.Width || g_fonts[f].table.page_height[p] != desc.Height) continue;
            float score = page_score(g_fonts[f].table, p, pixels, desc.Width, desc.Height);
            if (score > best) {
                best = score;
                best_font = f;
                best_page = p;
            }
        }
    }
    if (best < 0.8f) return;

    Font& font = g_fonts[best_font];
    logf("font %s page %d uploaded as texture %p (match %.2f)", font.table.name, best_page, (void*)destination, best);
    if (g_cfg.dev_commands) {
        char name[64];
        snprintf(name, sizeof name, "font_%s_%d", font.table.name, best_page);
        write_tga(name, pixels, desc.Width, desc.Height);
    }
    if (best_page != 0 || !font.patched) return;

    if (!font.replacement) {
        font.replacement = build_replacement(device, pixels, desc.Width, desc.Height);
        logf("icons: page for %s %s", font.table.name, font.replacement ? "built" : "could NOT be built");
    }
    if (font.replacement && g_live_count < MAX_LIVE && watch_release(destination)) g_live[g_live_count++] = {destination, font.replacement};
}

IDirect3DBaseTexture9* glyphs_substitute(IDirect3DBaseTexture9* texture) {
    for (int i = 0; i < g_live_count; i++)
        if (g_live[i].texture == texture) return g_live[i].replacement;
    return texture;
}
