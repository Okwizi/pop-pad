// SPDX-License-Identifier: GPL-3.0-or-later
#include "common.h"
#include "font_file.h"
#include <cstring>

typedef HANDLE(WINAPI* CreateFileA_t)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
static CreateFileA_t o_create_file;

static bool ends_with(const char* text, const char* suffix) {
    size_t n = strlen(text), m = strlen(suffix);
    return n >= m && lstrcmpiA(text + n - m, suffix) == 0;
}

bool fs_read_whole(const char* path, BYTE* buffer, DWORD capacity, DWORD* size) {
    HANDLE f = o_create_file(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    bool ok = ReadFile(f, buffer, capacity, size, nullptr) && *size < capacity;
    CloseHandle(f);
    return ok;
}

static HANDLE WINAPI hk_create_file(LPCSTR path, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES security, DWORD disposition, DWORD flags,
                                    HANDLE templ) {
    char replaced[MAX_PATH];
    if (path && g_cfg.glyphs && !(access & GENERIC_WRITE)) {
        if (ends_with(path, ".mft") && font_redirect(path, replaced, sizeof replaced))
            path = replaced;
        else if (ends_with(path, "POPData.bf") && names_redirect(path, replaced, sizeof replaced))
            path = replaced;
    }
    return o_create_file(path, access, share, security, disposition, flags, templ);
}

void fs_install(HMODULE exe) {
    // fs_read_whole is used before any hook exists, so it must work either way.
    o_create_file = CreateFileA;
    bool ok = iat_hook(exe, "KERNEL32.dll", "CreateFileA", (void*)hk_create_file, (void**)&o_create_file);
    logf("CreateFileA hook: %s", ok ? "installed" : "FAILED");
}
