// SPDX-License-Identifier: GPL-3.0-or-later
// A profile only gets gamepad bindings if an Xbox-type pad was recognised when it was created.
// Profiles made without one have none, so the pad does nothing and every prompt names a key.
// This adds the layout the game itself writes for a new profile to any profile that lacks it.
//
// Profile.DAT: "POP4", an 11-character UTF-16 name, then chunks of
//   u32 type, u32 size, u32 ~crc32(body), body.
// The bindings chunk (type 1) body is: 01 01, u32 count, count * 8-byte records, 8 bytes of
// stick/mouse speed. A record is: action, slot, device, vendor ID (u16), product ID (u16), code.
#include "common.h"
#include <cstdio>
#include <cstring>

enum { NAME_END = 0x1A, CHUNK_HEADER = 12, RECORD = 8, BINDINGS_CHUNK = 1 };
enum { DEVICE_GAMEPAD = 2, GAMEPAD_SLOT = 2, XBOX360_VID = 0x045E, XBOX360_PID = 0x028E };

struct DefaultBinding {
    BYTE action, code;
};

// Action numbers index the game's [Actions] names; codes are its input numbers for an Xbox pad.
static const DefaultBinding DEFAULTS[] = {
    {0, 62},                                // Roll/Jump/Action      A
    {1, 63},                                // Pick up/Throw/Drop    B
    {2, 64},                                // Dagger attack         X
    {3, 65},                                // Second weapon/Grab    Y
    {4, 66},                                // Landscape view        LT
    {5, 67},                                // First-person view     RT
    {6, 60},                                // Rewind/Slowdown       LB
    {7, 61},                                // Wall run/Block/Swing  RB
    {8, 57},                                // Navigation map        Back
    {9, 56},                                // Start                 Start
    {12, 52}, {13, 55}, {14, 53}, {15, 54}, // menu up, right, down, left on the D-pad
    {37, 70}, {38, 71}, {39, 68}, {40, 69}, // move left, right, forward, backward
    {41, 75}, {42, 74}, {43, 72}, {44, 73}, // camera left, right, forward, backward
};

static DWORD crc32(const BYTE* data, DWORD size) {
    DWORD crc = 0xFFFFFFFF;
    for (DWORD i = 0; i < size; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++) crc = (crc >> 1) ^ (0xEDB88320 & (0 - (crc & 1)));
    }
    return ~crc;
}

static DWORD read_u32(const BYTE* p) {
    DWORD v;
    memcpy(&v, p, 4);
    return v;
}
static void write_u32(BYTE* p, DWORD v) { memcpy(p, &v, 4); }

static bool read_file(const char* path, BYTE* buffer, DWORD capacity, DWORD* size) {
    HANDLE f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    bool ok = ReadFile(f, buffer, capacity, size, nullptr) && *size < capacity;
    CloseHandle(f);
    return ok;
}

static bool write_file(const char* path, const BYTE* data, DWORD size) {
    HANDLE f = CreateFileA(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD written;
    bool ok = WriteFile(f, data, size, &written, nullptr) && written == size;
    CloseHandle(f);
    return ok;
}

static void fix_profile(const char* path) {
    static BYTE in[16384], out[16384 + sizeof DEFAULTS * RECORD];
    DWORD size = 0;
    if (!read_file(path, in, sizeof in, &size) || size < NAME_END + CHUNK_HEADER || memcmp(in, "POP4", 4) != 0) {
        logf("profile %s: not a profile this version understands; left alone", path);
        return;
    }

    DWORD at = NAME_END;
    while (at + CHUNK_HEADER <= size && read_u32(in + at) != BINDINGS_CHUNK) at += CHUNK_HEADER + read_u32(in + at + 4);
    if (at + CHUNK_HEADER > size) {
        logf("profile %s: no bindings chunk; left alone", path);
        return;
    }

    DWORD body_size = read_u32(in + at + 4);
    const BYTE* body = in + at + CHUNK_HEADER;
    if (at + CHUNK_HEADER + body_size > size || body_size < 6 || read_u32(in + at + 8) != ~crc32(body, body_size)) {
        logf("profile %s: bindings chunk failed its checksum; left alone", path);
        return;
    }
    DWORD count = read_u32(body + 2);
    DWORD records_end = 6 + count * RECORD;
    if (records_end > body_size) {
        logf("profile %s: bindings chunk is malformed; left alone", path);
        return;
    }

    for (DWORD i = 0; i < count; i++) {
        const BYTE* r = body + 6 + i * RECORD;
        WORD vid, pid;
        memcpy(&vid, r + 3, 2);
        memcpy(&pid, r + 5, 2);
        if (r[2] == DEVICE_GAMEPAD && vid == XBOX360_VID && pid == XBOX360_PID) {
            logf("profile %s: already has Xbox pad bindings", path);
            return;
        }
    }

    const DWORD added = sizeof DEFAULTS / sizeof DEFAULTS[0];
    DWORD new_body_size = body_size + added * RECORD;
    BYTE* w = out;
    memcpy(w, in, at + CHUNK_HEADER + records_end);
    w += at + CHUNK_HEADER + records_end;
    for (const DefaultBinding& d : DEFAULTS) {
        const WORD vid = XBOX360_VID, pid = XBOX360_PID;
        w[0] = d.action;
        w[1] = GAMEPAD_SLOT;
        w[2] = DEVICE_GAMEPAD;
        memcpy(w + 3, &vid, 2);
        memcpy(w + 5, &pid, 2);
        w[7] = d.code;
        w += RECORD;
    }
    DWORD rest = size - (at + CHUNK_HEADER + records_end);
    memcpy(w, in + at + CHUNK_HEADER + records_end, rest);
    w += rest;

    BYTE* new_body = out + at + CHUNK_HEADER;
    write_u32(new_body + 2, count + added);
    write_u32(out + at + 4, new_body_size);
    write_u32(out + at + 8, ~crc32(new_body, new_body_size));

    char backup[MAX_PATH];
    snprintf(backup, sizeof backup, "%s.before-pop-pad", path);
    if (GetFileAttributesA(backup) == INVALID_FILE_ATTRIBUTES && !CopyFileA(path, backup, TRUE)) {
        logf("profile %s: could not write a backup; left alone", path);
        return;
    }
    bool ok = write_file(path, out, (DWORD)(w - out));
    logf("profile %s: %s %lu default Xbox pad bindings (backup: %s)", path, ok ? "added" : "FAILED to add", added, backup);
}

void profile_fix_all() {
    char root[MAX_PATH];
    lstrcpynA(root, g_dir, sizeof root);
    HKEY key;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Ubisoft\\Prince of Persia The Two Thrones\\1.00.999", 0, KEY_READ, &key) ==
        ERROR_SUCCESS) {
        char value[MAX_PATH];
        DWORD value_size = sizeof value - 2, type = 0;
        if (RegQueryValueExA(key, "Profiles_Path", nullptr, &type, (BYTE*)value, &value_size) == ERROR_SUCCESS && type == REG_SZ &&
            value_size > 1) {
            value[value_size] = 0;
            size_t n = strlen(value);
            if (n && value[n - 1] != '\\') {
                value[n] = '\\';
                value[n + 1] = 0;
            }
            lstrcpynA(root, value, sizeof root);
        }
        RegCloseKey(key);
    }

    char pattern[MAX_PATH];
    snprintf(pattern, sizeof pattern, "%sPOP3Profiles\\*", root);
    WIN32_FIND_DATAA found;
    HANDLE search = FindFirstFileA(pattern, &found);
    if (search == INVALID_HANDLE_VALUE) {
        logf("no profiles under %s", root);
        return;
    }
    do {
        if (!(found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || found.cFileName[0] == '.') continue;
        char path[MAX_PATH];
        snprintf(path, sizeof path, "%sPOP3Profiles\\%s\\Profile.DAT", root, found.cFileName);
        if (GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES) fix_profile(path);
    } while (FindNextFileA(search, &found));
    FindClose(search);
}
