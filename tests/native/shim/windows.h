// SPDX-License-Identifier: GPL-3.0-or-later
// Just enough of <windows.h> for the pure-logic parts of pop-pad (the font table, the name
// rewriting) to build and run on the machine that compiles the project. Nothing here is a
// general emulation: a function exists only because a tested file calls it.
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <strings.h>
#include <sys/stat.h>

typedef uint8_t BYTE;
typedef uint16_t WORD;
typedef uint32_t DWORD;
typedef int32_t LONG;
typedef unsigned int UINT;
typedef void* HANDLE;
typedef void* HMODULE;
typedef void* HWND;
typedef void* HINSTANCE;
typedef const char* LPCSTR;
typedef int BOOL;

#define MAX_PATH 260
#define GENERIC_WRITE 0x40000000u
#define CREATE_ALWAYS 2u
#define FILE_ATTRIBUTE_NORMAL 0x80u
#define INVALID_HANDLE_VALUE ((HANDLE)(intptr_t)-1)

// The game's paths use backslashes; the host's do not.
inline std::string host_path(const char* path) {
    std::string out = path;
    for (char& c : out)
        if (c == '\\') c = '/';
    return out;
}

inline int lstrcmpiA(const char* a, const char* b) { return strcasecmp(a, b); }

inline char* lstrcpynA(char* to, const char* from, int count) {
    if (count <= 0) return to;
    int i = 0;
    for (; i < count - 1 && from[i]; i++) to[i] = from[i];
    to[i] = 0;
    return to;
}

inline BOOL CreateDirectoryA(const char* path, void*) { return mkdir(host_path(path).c_str(), 0755) == 0; }

inline HANDLE CreateFileA(const char* path, DWORD, DWORD, void*, DWORD, DWORD, void*) {
    FILE* f = fopen(host_path(path).c_str(), "wb");
    return f ? (HANDLE)f : INVALID_HANDLE_VALUE;
}

inline BOOL WriteFile(HANDLE file, const void* data, DWORD size, DWORD* written, void*) {
    *written = (DWORD)fwrite(data, 1, size, (FILE*)file);
    return *written == size;
}

inline BOOL CloseHandle(HANDLE file) { return fclose((FILE*)file) == 0; }
