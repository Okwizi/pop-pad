// SPDX-License-Identifier: GPL-3.0-or-later
#include "common.h"
#include <cstdarg>
#include <cstdio>

static CRITICAL_SECTION g_lock;
static HANDLE g_file = INVALID_HANDLE_VALUE;

void log_init() {
    InitializeCriticalSection(&g_lock);
    if (!g_cfg.log) return;
    char path[MAX_PATH];
    snprintf(path, sizeof path, "%spoppad.log", g_dir);
    g_file = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
}

void logf(const char* fmt, ...) {
    if (g_file == INVALID_HANDLE_VALUE) return;
    char line[1024];
    int n = snprintf(line, sizeof line, "[%8lu] ", GetTickCount());
    va_list ap;
    va_start(ap, fmt);
    n += vsnprintf(line + n, sizeof line - n - 2, fmt, ap);
    va_end(ap);
    if (n > (int)sizeof line - 3) n = sizeof line - 3;
    line[n++] = '\r';
    line[n++] = '\n';
    EnterCriticalSection(&g_lock);
    DWORD written;
    WriteFile(g_file, line, n, &written, nullptr);
    LeaveCriticalSection(&g_lock);
}
