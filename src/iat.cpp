// SPDX-License-Identifier: GPL-3.0-or-later
#include "common.h"
#include <cstring>

static void write_ptr(void** slot, void* value) {
    DWORD old;
    VirtualProtect(slot, sizeof(void*), PAGE_EXECUTE_READWRITE, &old);
    *slot = value;
    VirtualProtect(slot, sizeof(void*), old, &old);
}

bool iat_hook(HMODULE mod, const char* dll, const char* func, void* repl, void** orig) {
    BYTE* base = (BYTE*)mod;
    auto* dos = (IMAGE_DOS_HEADER*)base;
    auto* nt = (IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
    auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress) return false;

    for (auto* imp = (IMAGE_IMPORT_DESCRIPTOR*)(base + dir.VirtualAddress); imp->Name; ++imp) {
        if (lstrcmpiA((const char*)(base + imp->Name), dll) != 0) continue;
        if (!imp->OriginalFirstThunk) return false;
        auto* names = (IMAGE_THUNK_DATA*)(base + imp->OriginalFirstThunk);
        auto* slots = (IMAGE_THUNK_DATA*)(base + imp->FirstThunk);
        for (; names->u1.AddressOfData; ++names, ++slots) {
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
            auto* by_name = (IMAGE_IMPORT_BY_NAME*)(base + names->u1.AddressOfData);
            if (strcmp((const char*)by_name->Name, func) != 0) continue;
            if (orig) *orig = (void*)slots->u1.Function;
            write_ptr((void**)&slots->u1.Function, repl);
            return true;
        }
    }
    return false;
}

// Returns the previous entry, or null when the slot already holds repl (a vtable is shared by
// every object of its class, so a second object must not overwrite the saved original).
void* vt_patch(void* obj, int index, void* repl) {
    void** vt = *(void***)obj;
    if (vt[index] == repl) return nullptr;
    void* old = vt[index];
    write_ptr(&vt[index], repl);
    return old;
}
