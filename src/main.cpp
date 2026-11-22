// SPDX-License-Identifier: GPL-3.0-or-later
#include "common.h"
#include "icons.h"
#include <unknwn.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

Config g_cfg;
char g_dir[MAX_PATH];

static HMODULE g_real;

static FARPROC real_proc(const char* name) {
    if (!g_real) {
        char path[MAX_PATH];
        UINT n = GetSystemDirectoryA(path, MAX_PATH);
        snprintf(path + n, MAX_PATH - n, "\\dinput8.dll");
        g_real = LoadLibraryA(path);
        logf("system dinput8: %s -> %p", path, (void*)g_real);
    }
    return g_real ? GetProcAddress(g_real, name) : nullptr;
}

extern "C" HRESULT WINAPI PP_DirectInput8Create(HINSTANCE inst, DWORD version, REFIID riid, LPVOID* out, LPUNKNOWN outer) {
    typedef HRESULT(WINAPI * Fn)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
    Fn fn = (Fn)real_proc("DirectInput8Create");
    if (!fn) return E_FAIL;
    HRESULT hr = fn(inst, version, riid, out, outer);
    logf("DirectInput8Create version=%lx iid=%08lX hr=%08lx", version, riid.Data1, hr);
    return hr;
}

typedef HRESULT(WINAPI* DirectInput8Create_t)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
static DirectInput8Create_t o_imported_create;

// Steam's overlay detours this DLL's DirectInput8Create export and installs its own device
// filter on the interface before the call returns. Hooking the game's import instead, and
// patching the interface only after that call has returned, keeps our hooks on the outside:
// game -> pop-pad -> Steam -> DirectInput. Patching any earlier would let Steam save our hook
// as "the original" underneath its own.
static HRESULT WINAPI imported_create(HINSTANCE inst, DWORD version, REFIID riid, LPVOID* out, LPUNKNOWN outer) {
    HRESULT hr = o_imported_create(inst, version, riid, out, outer);
    if (SUCCEEDED(hr) && out && *out) {
        const DWORD IID_DI8A_DATA1 = 0xBF798030;
        if (riid.Data1 == IID_DI8A_DATA1)
            di_hook_interface(*out);
        else
            logf("game asked for a non-ANSI DirectInput interface; device hooks skipped");
    }
    return hr;
}

extern "C" HRESULT WINAPI PP_DllCanUnloadNow() {
    typedef HRESULT(WINAPI * Fn)();
    Fn fn = (Fn)real_proc("DllCanUnloadNow");
    return fn ? fn() : S_FALSE;
}

extern "C" HRESULT WINAPI PP_DllGetClassObject(REFCLSID clsid, REFIID riid, LPVOID* out) {
    typedef HRESULT(WINAPI * Fn)(REFCLSID, REFIID, LPVOID*);
    Fn fn = (Fn)real_proc("DllGetClassObject");
    return fn ? fn(clsid, riid, out) : CLASS_E_CLASSNOTAVAILABLE;
}

extern "C" HRESULT WINAPI PP_DllRegisterServer() {
    typedef HRESULT(WINAPI * Fn)();
    Fn fn = (Fn)real_proc("DllRegisterServer");
    return fn ? fn() : E_FAIL;
}

extern "C" HRESULT WINAPI PP_DllUnregisterServer() {
    typedef HRESULT(WINAPI * Fn)();
    Fn fn = (Fn)real_proc("DllUnregisterServer");
    return fn ? fn() : E_FAIL;
}

extern "C" BOOL WINAPI DllMain(HINSTANCE self, DWORD reason, LPVOID) {
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    DisableThreadLibraryCalls(self);

    GetModuleFileNameA(nullptr, g_dir, MAX_PATH);
    char* slash = strrchr(g_dir, '\\');
    if (slash) slash[1] = 0;

    char ini[MAX_PATH];
    snprintf(ini, sizeof ini, "%spoppad.ini", g_dir);
    g_cfg.log = GetPrivateProfileIntA("General", "Log", 0, ini) != 0;
    g_cfg.native_xinput = GetPrivateProfileIntA("Input", "NativeXInput", 1, ini) != 0;
    g_cfg.add_default_bindings = GetPrivateProfileIntA("Input", "AddDefaultBindings", 1, ini) != 0;
    g_cfg.stick_saturation = GetPrivateProfileIntA("Input", "StickSaturation", 75, ini);
    if (g_cfg.stick_saturation < 30 || g_cfg.stick_saturation > 100) g_cfg.stick_saturation = 100;
    g_cfg.glyphs = GetPrivateProfileIntA("Glyphs", "Enabled", 1, ini) != 0;
    g_cfg.dev_virtual_pad = GetPrivateProfileIntA("Dev", "VirtualPad", 0, ini) != 0;
    g_cfg.dev_commands = GetPrivateProfileIntA("Dev", "Commands", 0, ini) != 0;
    g_cfg.dev_probe = GetPrivateProfileIntA("Dev", "Probe", 0, ini) != 0;
    char icon_test[16];
    GetPrivateProfileStringA("Dev", "IconTest", "0", icon_test, sizeof icon_test, ini);
    g_cfg.dev_icon_test = (int)strtoul(icon_test, nullptr, 16) & 0xFF;

    log_init();
    logf("pop-pad loaded; native_xinput=%d virtual_pad=%d commands=%d", g_cfg.native_xinput, g_cfg.dev_virtual_pad, g_cfg.dev_commands);

    if (g_cfg.native_xinput && g_cfg.add_default_bindings) profile_fix_all();

    HMODULE exe = GetModuleHandleA(nullptr);
    bool ok = iat_hook(exe, "DINPUT8.dll", "DirectInput8Create", (void*)imported_create, (void**)&o_imported_create);
    logf("DirectInput8Create import hook: %s", ok ? "installed" : "FAILED");
    xi_install(exe);
    fs_install(exe);
    if (g_cfg.glyphs) g_cfg.glyphs = glyphs_init();
    if (g_cfg.glyphs) brackets_install();
    d3d_install(exe);
    return TRUE;
}
