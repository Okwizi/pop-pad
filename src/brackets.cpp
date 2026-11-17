// SPDX-License-Identifier: GPL-3.0-or-later
// The game wraps the name of every input in "[ ]" before it goes into a prompt, in two routines
// of POP3.EXE (one for the normal and long names, one for the shortest). They are detoured here
// so that a name which is a single icon character comes back bare; every other name, keyboard
// keys included, keeps its brackets.
//
// This is the one part of pop-pad tied to an exact game build. The routines are recognised by
// their bytes, and a build that does not match is left alone: icons then stay inside brackets.
#include "common.h"
#include "icons.h"
#include <cstring>

typedef char*(__thiscall* LongName)(void* self, DWORD device, DWORD code, DWORD long_form);
typedef char*(__thiscall* ShortName)(void* self, DWORD device, DWORD code);
static LongName g_long_name;
static ShortName g_short_name;

static char* unbracket(char* name) {
    if (name[0] != '[' || name[2] != ']' || name[3]) return name;
    for (BYTE icon : ICON_CHARS) {
        if ((BYTE)name[1] != icon) continue;
        name[0] = name[1];
        name[1] = 0;
        break;
    }
    return name;
}

static char* __thiscall hk_long_name(void* self, DWORD device, DWORD code, DWORD long_form) {
    return unbracket(g_long_name(self, device, code, long_form));
}

static char* __thiscall hk_short_name(void* self, DWORD device, DWORD code) { return unbracket(g_short_name(self, device, code)); }

// Both routines open with "push -1; push <handler>" (7 bytes that can run from anywhere) and
// later store "[" into their result buffer.
enum { HEAD_SIZE = 7, STORE_SIZE = 9 };
struct Routine {
    DWORD address;
    BYTE head[HEAD_SIZE];
    DWORD store_offset;
    BYTE store[STORE_SIZE];
};
static const Routine LONG_NAME = {
    0x41b7e0,
    {0x6A, 0xFF, 0x68, 0x36, 0x26, 0x97, 0x00},
    0x76,
    {0x66, 0xC7, 0x05, 0x28, 0x29, 0xA0, 0x00, 0x5B, 0x00},
};
static const Routine SHORT_NAME = {
    0x41bb80,
    {0x6A, 0xFF, 0x68, 0x50, 0x26, 0x97, 0x00},
    0x59,
    {0x66, 0xC7, 0x05, 0x30, 0x2E, 0xA0, 0x00, 0x5B, 0x00},
};

static bool recognised(const Routine& routine) {
    const BYTE* code = (const BYTE*)routine.address;
    if (IsBadReadPtr(code, routine.store_offset + STORE_SIZE)) return false;
    return !memcmp(code, routine.head, HEAD_SIZE) && !memcmp(code + routine.store_offset, routine.store, STORE_SIZE);
}

static void put_jump(BYTE* at, const void* to) {
    at[0] = 0xE9;
    LONG distance = (LONG)((const BYTE*)to - (at + 5));
    memcpy(at + 1, &distance, 4);
}

// Returns a stub that runs the routine's displaced first instructions and continues in it.
static void* original_entry(const Routine& routine) {
    BYTE* stub = (BYTE*)VirtualAlloc(nullptr, HEAD_SIZE + 5, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!stub) return nullptr;
    memcpy(stub, routine.head, HEAD_SIZE);
    put_jump(stub + HEAD_SIZE, (const BYTE*)routine.address + HEAD_SIZE);
    return stub;
}

static bool divert(const Routine& routine, const void* hook) {
    BYTE* code = (BYTE*)routine.address;
    DWORD protection;
    if (!VirtualProtect(code, HEAD_SIZE, PAGE_EXECUTE_READWRITE, &protection)) return false;
    put_jump(code, hook);
    memset(code + 5, 0x90, HEAD_SIZE - 5);
    VirtualProtect(code, HEAD_SIZE, protection, &protection);
    FlushInstructionCache(GetCurrentProcess(), code, HEAD_SIZE);
    return true;
}

void brackets_install() {
    if (!recognised(LONG_NAME) || !recognised(SHORT_NAME)) {
        logf("brackets: this game build is not the one pop-pad knows; icons keep their brackets");
        return;
    }
    g_long_name = (LongName)original_entry(LONG_NAME);
    g_short_name = (ShortName)original_entry(SHORT_NAME);
    if (!g_long_name || !g_short_name) return;
    bool ok = divert(LONG_NAME, (const void*)hk_long_name) && divert(SHORT_NAME, (const void*)hk_short_name);
    logf("brackets: %s", ok ? "icons are shown without brackets" : "could NOT be removed");
}
