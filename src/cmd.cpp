// SPDX-License-Identifier: GPL-3.0-or-later
// Development command channel: a script dropped next to the game as poppad.cmd is run line by
// line, so the game can be driven and screenshotted from another machine. Off unless
// [Dev] Commands=1.
#include "common.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

struct ButtonName {
    const char* name;
    WORD mask;
};

static const ButtonName BUTTONS[] = {
    {"UP", XINPUT_GAMEPAD_DPAD_UP},
    {"DOWN", XINPUT_GAMEPAD_DPAD_DOWN},
    {"LEFT", XINPUT_GAMEPAD_DPAD_LEFT},
    {"RIGHT", XINPUT_GAMEPAD_DPAD_RIGHT},
    {"START", XINPUT_GAMEPAD_START},
    {"BACK", XINPUT_GAMEPAD_BACK},
    {"LS", XINPUT_GAMEPAD_LEFT_THUMB},
    {"RS", XINPUT_GAMEPAD_RIGHT_THUMB},
    {"LB", XINPUT_GAMEPAD_LEFT_SHOULDER},
    {"RB", XINPUT_GAMEPAD_RIGHT_SHOULDER},
    {"A", XINPUT_GAMEPAD_A},
    {"B", XINPUT_GAMEPAD_B},
    {"X", XINPUT_GAMEPAD_X},
    {"Y", XINPUT_GAMEPAD_Y},
};

static bool set_button(const char* name, bool down) {
    XINPUT_GAMEPAD pad = vpad_get();
    if (!lstrcmpiA(name, "LT"))
        pad.bLeftTrigger = down ? 255 : 0;
    else if (!lstrcmpiA(name, "RT"))
        pad.bRightTrigger = down ? 255 : 0;
    else {
        WORD mask = 0;
        for (const ButtonName& b : BUTTONS)
            if (!lstrcmpiA(name, b.name)) mask = b.mask;
        if (!mask) return false;
        pad.wButtons = down ? (pad.wButtons | mask) : (pad.wButtons & ~mask);
    }
    vpad_set(pad);
    return true;
}

static void send_key(WORD scan, bool extended) {
    INPUT in = {};
    in.type = INPUT_KEYBOARD;
    in.ki.wScan = scan;
    in.ki.dwFlags = KEYEVENTF_SCANCODE | (extended ? KEYEVENTF_EXTENDEDKEY : 0);
    SendInput(1, &in, sizeof in);
    Sleep(90);
    in.ki.dwFlags |= KEYEVENTF_KEYUP;
    SendInput(1, &in, sizeof in);
}

static bool g_quit;

static void run_line(char* line, char* report, size_t report_size) {
    char verb[32] = "", a[64] = "", b[32] = "", c[32] = "";
    int n = sscanf(line, "%31s %63s %31s %31s", verb, a, b, c);
    if (n < 1 || verb[0] == '#') return;
    size_t used = strlen(report);
    char* tail = report + used;
    size_t room = report_size - used;

    if (!strcmp(verb, "sleep")) {
        Sleep(atoi(a));
    } else if (!strcmp(verb, "tap")) {
        int ms = n >= 3 ? atoi(b) : 120;
        if (!set_button(a, true)) {
            snprintf(tail, room, "unknown button %s\r\n", a);
            return;
        }
        Sleep(ms);
        set_button(a, false);
        Sleep(150);
    } else if (!strcmp(verb, "hold")) {
        if (!set_button(a, true)) snprintf(tail, room, "unknown button %s\r\n", a);
    } else if (!strcmp(verb, "release")) {
        if (!set_button(a, false)) snprintf(tail, room, "unknown button %s\r\n", a);
    } else if (!strcmp(verb, "stick")) {
        XINPUT_GAMEPAD pad = vpad_get();
        SHORT x = (SHORT)atoi(b), y = (SHORT)atoi(c);
        if (a[0] == 'L' || a[0] == 'l') {
            pad.sThumbLX = x;
            pad.sThumbLY = y;
        } else {
            pad.sThumbRX = x;
            pad.sThumbRY = y;
        }
        vpad_set(pad);
    } else if (!strcmp(verb, "key")) {
        send_key((WORD)strtoul(a, nullptr, 16), n >= 3 && b[0] == 'e');
        Sleep(150);
    } else if (!strcmp(verb, "shot")) {
        bool ok = shot_request(a, 5000);
        snprintf(tail, room, "shot %s %s\r\n", a, ok ? "ok" : "FAILED");
    } else if (!strcmp(verb, "quit")) {
        g_quit = true;
    } else if (strcmp(verb, "end")) {
        snprintf(tail, room, "unknown command %s\r\n", verb);
    }
}

// A script is complete only once its closing "end" line has arrived; until then the file is
// still being copied over and must be left alone.
static bool has_end_line(const char* text) {
    for (const char* p = text; *p;) {
        const char* eol = p + strcspn(p, "\r\n");
        if (eol - p == 3 && !strncmp(p, "end", 3)) return true;
        p = eol + strspn(eol, "\r\n");
    }
    return false;
}

static DWORD WINAPI worker(void*) {
    char cmd_path[MAX_PATH], done_path[MAX_PATH];
    snprintf(cmd_path, sizeof cmd_path, "%spoppad.cmd", g_dir);
    snprintf(done_path, sizeof done_path, "%spoppad.done", g_dir);
    logf("command channel watching %s", cmd_path);

    static char script[16384];
    static char report[4096];
    for (;;) {
        Sleep(100);
        HANDLE f = CreateFileA(cmd_path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (f == INVALID_HANDLE_VALUE) continue;
        DWORD size = 0;
        ReadFile(f, script, sizeof script - 1, &size, nullptr);
        CloseHandle(f);
        script[size] = 0;
        if (!has_end_line(script)) continue;
        DeleteFileA(cmd_path);
        DeleteFileA(done_path);

        report[0] = 0;
        for (char* line = script; *line;) {
            char* eol = line + strcspn(line, "\r\n");
            char* next = eol + strspn(eol, "\r\n");
            *eol = 0;
            run_line(line, report, sizeof report);
            line = next;
        }
        strncat(report, "done\r\n", sizeof report - strlen(report) - 1);

        f = CreateFileA(done_path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (f != INVALID_HANDLE_VALUE) {
            DWORD written;
            WriteFile(f, report, (DWORD)strlen(report), &written, nullptr);
            CloseHandle(f);
        }
        if (g_quit) {
            logf("quit requested");
            PostMessageA(game_window(), WM_CLOSE, 0, 0);
            Sleep(5000);
            // Still alive: force it, but clear the marker the game leaves while running, or its
            // next start stops at a "use safe mode settings?" prompt.
            char marker[MAX_PATH];
            snprintf(marker, sizeof marker, "%sPrinceSM", g_dir);
            DeleteFileA(marker);
            TerminateProcess(GetCurrentProcess(), 0);
        }
    }
}

void cmd_start() { CloseHandle(CreateThread(nullptr, 0, worker, nullptr, 0, nullptr)); }
