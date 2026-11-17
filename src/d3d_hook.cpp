// SPDX-License-Identifier: GPL-3.0-or-later
#include "common.h"
#include <d3d9.h>
#include <cstdio>
#include <cstring>

typedef IDirect3D9*(WINAPI* Create9_t)(UINT);
typedef HRESULT(STDMETHODCALLTYPE* CreateDevice_t)(IDirect3D9*, UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS*, IDirect3DDevice9**);
typedef HRESULT(STDMETHODCALLTYPE* Present_t)(IDirect3DDevice9*, const RECT*, const RECT*, HWND, const RGNDATA*);
typedef HRESULT(STDMETHODCALLTYPE* UpdateTexture_t)(IDirect3DDevice9*, IDirect3DBaseTexture9*, IDirect3DBaseTexture9*);
typedef HRESULT(STDMETHODCALLTYPE* SetTexture_t)(IDirect3DDevice9*, DWORD, IDirect3DBaseTexture9*);
typedef HRESULT(STDMETHODCALLTYPE* Reset_t)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);

enum { VT_D3D_CREATE_DEVICE = 16, VT_DEV_RESET = 16, VT_DEV_PRESENT = 17, VT_DEV_UPDATE_TEXTURE = 31, VT_DEV_SET_TEXTURE = 65 };
enum { SHOT_MAX_WIDTH = 1920 };

static Create9_t o_create9;
static CreateDevice_t o_create_device;
static Present_t o_present;
static UpdateTexture_t o_update_texture;
static SetTexture_t o_set_texture;
static Reset_t o_reset;

static HWND g_window;
static char g_shot_name[64];
static volatile LONG g_shot_pending;
static HANDLE g_shot_done;
static bool g_shot_ok;

HWND game_window() { return g_window; }

bool shot_request(const char* name, DWORD timeout_ms) {
    if (!g_shot_done) return false;
    lstrcpynA(g_shot_name, name, sizeof g_shot_name);
    ResetEvent(g_shot_done);
    g_shot_ok = false;
    InterlockedExchange(&g_shot_pending, 1);
    return WaitForSingleObject(g_shot_done, timeout_ms) == WAIT_OBJECT_0 && g_shot_ok;
}

static void read_pixel(const BYTE* p, D3DFORMAT format, unsigned& r, unsigned& g, unsigned& b) {
    if (format == D3DFMT_R5G6B5) {
        WORD v = *(const WORD*)p;
        r = ((v >> 11) & 31) * 255 / 31;
        g = ((v >> 5) & 63) * 255 / 63;
        b = (v & 31) * 255 / 31;
    } else {
        b = p[0];
        g = p[1];
        r = p[2];
    }
}

// Writes a 24-bit BMP, box-filtered down so a 4K frame stays small enough to copy off the PC.
static bool write_bmp(const char* name, const D3DLOCKED_RECT& lock, const D3DSURFACE_DESC& desc) {
    int bytes_per_pixel = desc.Format == D3DFMT_R5G6B5 ? 2 : 4;
    if (desc.Format != D3DFMT_R5G6B5 && desc.Format != D3DFMT_X8R8G8B8 && desc.Format != D3DFMT_A8R8G8B8) {
        logf("screenshot: unsupported back buffer format %d", (int)desc.Format);
        return false;
    }
    int factor = (desc.Width + SHOT_MAX_WIDTH - 1) / SHOT_MAX_WIDTH;
    if (factor < 1) factor = 1;
    int w = desc.Width / factor, h = desc.Height / factor;
    int stride = (w * 3 + 3) & ~3;
    DWORD image_size = stride * h;
    BYTE* out = (BYTE*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, image_size);
    if (!out) return false;

    for (int y = 0; y < h; y++) {
        BYTE* row = out + (h - 1 - y) * stride;
        for (int x = 0; x < w; x++) {
            unsigned sr = 0, sg = 0, sb = 0;
            for (int dy = 0; dy < factor; dy++) {
                const BYTE* src = (const BYTE*)lock.pBits + (y * factor + dy) * lock.Pitch + x * factor * bytes_per_pixel;
                for (int dx = 0; dx < factor; dx++, src += bytes_per_pixel) {
                    unsigned r, g, b;
                    read_pixel(src, desc.Format, r, g, b);
                    sr += r;
                    sg += g;
                    sb += b;
                }
            }
            unsigned n = factor * factor;
            row[x * 3 + 0] = (BYTE)(sb / n);
            row[x * 3 + 1] = (BYTE)(sg / n);
            row[x * 3 + 2] = (BYTE)(sr / n);
        }
    }

    char path[MAX_PATH];
    snprintf(path, sizeof path, "%spoppad_shots", g_dir);
    CreateDirectoryA(path, nullptr);
    snprintf(path, sizeof path, "%spoppad_shots\\%s.bmp", g_dir, name);

    BITMAPFILEHEADER file_header = {};
    BITMAPINFOHEADER info = {};
    file_header.bfType = 0x4D42;
    file_header.bfOffBits = sizeof file_header + sizeof info;
    file_header.bfSize = file_header.bfOffBits + image_size;
    info.biSize = sizeof info;
    info.biWidth = w;
    info.biHeight = h;
    info.biPlanes = 1;
    info.biBitCount = 24;
    info.biSizeImage = image_size;

    bool ok = false;
    HANDLE f = CreateFileA(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f != INVALID_HANDLE_VALUE) {
        DWORD written;
        ok = WriteFile(f, &file_header, sizeof file_header, &written, nullptr) && WriteFile(f, &info, sizeof info, &written, nullptr) &&
             WriteFile(f, out, image_size, &written, nullptr);
        CloseHandle(f);
    }
    HeapFree(GetProcessHeap(), 0, out);
    logf("screenshot %s: %ux%u -> %dx%d %s", name, desc.Width, desc.Height, w, h, ok ? "ok" : "FAILED");
    return ok;
}

static bool capture(IDirect3DDevice9* dev, const char* name) {
    IDirect3DSurface9* back = nullptr;
    if (FAILED(dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &back))) return false;
    D3DSURFACE_DESC desc;
    back->GetDesc(&desc);

    // GetRenderTargetData refuses multisampled surfaces, so those are resolved into a plain one first.
    IDirect3DSurface9* resolved = nullptr;
    IDirect3DSurface9* source = back;
    if (desc.MultiSampleType != D3DMULTISAMPLE_NONE) {
        if (SUCCEEDED(dev->CreateRenderTarget(desc.Width, desc.Height, desc.Format, D3DMULTISAMPLE_NONE, 0, FALSE, &resolved, nullptr)) &&
            SUCCEEDED(dev->StretchRect(back, nullptr, resolved, nullptr, D3DTEXF_NONE))) {
            source = resolved;
        } else {
            source = nullptr;
        }
    }

    bool ok = false;
    IDirect3DSurface9* copy = nullptr;
    if (source && SUCCEEDED(dev->CreateOffscreenPlainSurface(desc.Width, desc.Height, desc.Format, D3DPOOL_SYSTEMMEM, &copy, nullptr))) {
        D3DLOCKED_RECT lock;
        if (SUCCEEDED(dev->GetRenderTargetData(source, copy)) && SUCCEEDED(copy->LockRect(&lock, nullptr, D3DLOCK_READONLY))) {
            ok = write_bmp(name, lock, desc);
            copy->UnlockRect();
        }
    }
    if (!ok) logf("screenshot %s: capture failed (format %d, multisample %d)", name, (int)desc.Format, (int)desc.MultiSampleType);

    if (copy) copy->Release();
    if (resolved) resolved->Release();
    back->Release();
    return ok;
}

static HRESULT STDMETHODCALLTYPE hk_present(IDirect3DDevice9* dev, const RECT* src, const RECT* dst, HWND window, const RGNDATA* dirty) {
    static bool first = true;
    if (first) {
        first = false;
        logf("first frame presented");
        if (g_cfg.dev_commands) cmd_start();
    }
    if (InterlockedExchange(&g_shot_pending, 0)) {
        g_shot_ok = capture(dev, g_shot_name);
        SetEvent(g_shot_done);
    }
    return o_present(dev, src, dst, window, dirty);
}

static HRESULT STDMETHODCALLTYPE hk_update_texture(IDirect3DDevice9* dev, IDirect3DBaseTexture9* source,
                                                   IDirect3DBaseTexture9* destination) {
    if (source && destination) glyphs_texture_uploaded(dev, source, destination);
    return o_update_texture(dev, source, destination);
}

static HRESULT STDMETHODCALLTYPE hk_set_texture(IDirect3DDevice9* dev, DWORD stage, IDirect3DBaseTexture9* texture) {
    return o_set_texture(dev, stage, texture ? glyphs_substitute(texture) : texture);
}

static HRESULT STDMETHODCALLTYPE hk_reset(IDirect3DDevice9* dev, D3DPRESENT_PARAMETERS* pp) {
    glyphs_device_reset();
    return o_reset(dev, pp);
}

static HRESULT STDMETHODCALLTYPE hk_create_device(IDirect3D9* self, UINT adapter, D3DDEVTYPE type, HWND focus, DWORD flags,
                                                  D3DPRESENT_PARAMETERS* pp, IDirect3DDevice9** out) {
    HRESULT hr = o_create_device(self, adapter, type, focus, flags, pp, out);
    if (pp)
        logf("CreateDevice %ux%u windowed=%d format=%d hr=%08lx", pp->BackBufferWidth, pp->BackBufferHeight, pp->Windowed,
             (int)pp->BackBufferFormat, hr);
    if (SUCCEEDED(hr) && out && *out) {
        g_window = (pp && pp->hDeviceWindow) ? pp->hDeviceWindow : focus;
        void* previous = vt_patch(*out, VT_DEV_PRESENT, (void*)hk_present);
        if (previous) o_present = (Present_t)previous;
        if (g_cfg.glyphs) {
            previous = vt_patch(*out, VT_DEV_UPDATE_TEXTURE, (void*)hk_update_texture);
            if (previous) o_update_texture = (UpdateTexture_t)previous;
            previous = vt_patch(*out, VT_DEV_SET_TEXTURE, (void*)hk_set_texture);
            if (previous) o_set_texture = (SetTexture_t)previous;
            previous = vt_patch(*out, VT_DEV_RESET, (void*)hk_reset);
            if (previous) o_reset = (Reset_t)previous;
        }
    }
    return hr;
}

static IDirect3D9* WINAPI hk_create9(UINT sdk_version) {
    IDirect3D9* d3d = o_create9(sdk_version);
    logf("Direct3DCreate9 -> %p", (void*)d3d);
    if (d3d) {
        void* previous = vt_patch(d3d, VT_D3D_CREATE_DEVICE, (void*)hk_create_device);
        if (previous) o_create_device = (CreateDevice_t)previous;
    }
    return d3d;
}

void d3d_install(HMODULE exe) {
    if (!g_cfg.dev_commands && !g_cfg.glyphs) return;
    g_shot_done = CreateEventA(nullptr, TRUE, FALSE, nullptr);
    bool ok = iat_hook(exe, "d3d9.dll", "Direct3DCreate9", (void*)hk_create9, (void**)&o_create9);
    logf("Direct3DCreate9 hook: %s", ok ? "installed" : "FAILED");
}
