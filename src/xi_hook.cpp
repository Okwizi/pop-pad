// SPDX-License-Identifier: GPL-3.0-or-later
#include "common.h"
#include <cstring>

typedef DWORD(WINAPI* GetState_t)(DWORD, XINPUT_STATE*);
typedef DWORD(WINAPI* SetState_t)(DWORD, XINPUT_VIBRATION*);
typedef DWORD(WINAPI* GetCapabilities_t)(DWORD, DWORD, XINPUT_CAPABILITIES*);
typedef FARPROC(WINAPI* GetProcAddress_t)(HMODULE, LPCSTR);

static GetProcAddress_t o_get_proc_address;
static GetState_t r_get_state;
static SetState_t r_set_state;
static GetCapabilities_t r_get_capabilities;

static XINPUT_GAMEPAD g_vpad;
static volatile LONG g_vpad_packet = 1;

void vpad_set(const XINPUT_GAMEPAD& pad) {
    g_vpad = pad;
    InterlockedIncrement(&g_vpad_packet);
}

XINPUT_GAMEPAD vpad_get() { return g_vpad; }

static bool is_vpad(DWORD index) { return g_cfg.dev_virtual_pad && index == 0; }

// The game asks for the pad by a DirectInput joystick number, which need not match the XInput
// slot the pad actually sits in. Its one pad is therefore served from whichever slot is live.
static DWORD g_slot;
static DWORD g_next_scan;

static DWORD live_slot(DWORD index) {
    if (!g_cfg.native_xinput || index != 0 || !r_get_state) return index;
    XINPUT_STATE probe;
    if (r_get_state(g_slot, &probe) == ERROR_SUCCESS) return g_slot;
    DWORD now = GetTickCount();
    if ((LONG)(now - g_next_scan) < 0) return g_slot;
    g_next_scan = now + 1000;
    for (DWORD slot = 0; slot < XUSER_MAX_COUNT; slot++) {
        if (r_get_state(slot, &probe) != ERROR_SUCCESS) continue;
        if (slot != g_slot) logf("XInput pad found in slot %lu", slot);
        g_slot = slot;
        break;
    }
    return g_slot;
}

// The game reads each stick axis on its own and wants a full-scale value to run, which a round
// stick gate never delivers on a diagonal. Stretching the range lets diagonals reach full speed.
static SHORT stretch(SHORT value) {
    LONG scaled = (LONG)value * 100 / g_cfg.stick_saturation;
    return (SHORT)(scaled > 32767 ? 32767 : scaled < -32768 ? -32768 : scaled);
}

static void adjust(XINPUT_GAMEPAD& pad) {
    if (g_cfg.stick_saturation >= 100) return;
    pad.sThumbLX = stretch(pad.sThumbLX);
    pad.sThumbLY = stretch(pad.sThumbLY);
    pad.sThumbRX = stretch(pad.sThumbRX);
    pad.sThumbRY = stretch(pad.sThumbRY);
}

// Why the game sees no input under Steam Input: log each change in what the pad reports, and
// every few seconds how often the game polls and which slots hold a pad.
static void probe_state(DWORD asked, DWORD result, const XINPUT_STATE* state) {
    static DWORD calls, last_result = 0xFFFFFFFF, last_packet, next_report, changes, reports;
    calls++;
    bool changed = result != last_result || (result == ERROR_SUCCESS && state->dwPacketNumber != last_packet);
    if (changed && changes < 300) {
        changes++;
        const XINPUT_GAMEPAD& g = state->Gamepad;
        logf("probe: asked=%lu slot=%lu result=%lu packet=%lu buttons=%04X lt=%u rt=%u lx=%d ly=%d", asked, g_slot, result,
             state->dwPacketNumber, g.wButtons, g.bLeftTrigger, g.bRightTrigger, g.sThumbLX, g.sThumbLY);
    }
    last_result = result;
    if (result == ERROR_SUCCESS) last_packet = state->dwPacketNumber;
    DWORD now = GetTickCount();
    if ((LONG)(now - next_report) < 0 || reports >= 120) return;
    next_report = now + 5000;
    reports++;
    DWORD connected = 0;
    XINPUT_STATE other;
    for (DWORD slot = 0; slot < XUSER_MAX_COUNT; slot++)
        if (r_get_state(slot, &other) == ERROR_SUCCESS) connected |= 1u << slot;
    logf("probe: %lu polls so far, last result %lu, connected slots mask %lX", calls, last_result, connected);
}

static DWORD WINAPI my_get_state(DWORD index, XINPUT_STATE* state) {
    static LONG calls;
    LONG n = InterlockedIncrement(&calls);
    if (is_vpad(index)) {
        state->dwPacketNumber = (DWORD)g_vpad_packet;
        state->Gamepad = g_vpad;
        if (n == 1) logf("XInputGetState(%lu): serving the virtual pad", index);
        return ERROR_SUCCESS;
    }
    DWORD result = r_get_state ? r_get_state(live_slot(index), state) : ERROR_DEVICE_NOT_CONNECTED;
    if (result == ERROR_SUCCESS) adjust(state->Gamepad);
    if (n <= 3) logf("XInputGetState(%lu) -> %lu", index, result);
    if (g_cfg.dev_probe && r_get_state) probe_state(index, result, state);
    return result;
}

static DWORD WINAPI my_set_state(DWORD index, XINPUT_VIBRATION* vibration) {
    if (is_vpad(index)) return ERROR_SUCCESS;
    return r_set_state ? r_set_state(live_slot(index), vibration) : ERROR_DEVICE_NOT_CONNECTED;
}

static DWORD WINAPI my_get_capabilities(DWORD index, DWORD flags, XINPUT_CAPABILITIES* caps) {
    if (is_vpad(index)) {
        memset(caps, 0, sizeof *caps);
        caps->Type = XINPUT_DEVTYPE_GAMEPAD;
        caps->SubType = XINPUT_DEVSUBTYPE_GAMEPAD;
        caps->Gamepad.wButtons = 0xF3FF;
        caps->Gamepad.bLeftTrigger = caps->Gamepad.bRightTrigger = 255;
        caps->Gamepad.sThumbLX = caps->Gamepad.sThumbLY = (SHORT)0xFFC0;
        caps->Gamepad.sThumbRX = caps->Gamepad.sThumbRY = (SHORT)0xFFC0;
        return ERROR_SUCCESS;
    }
    return r_get_capabilities ? r_get_capabilities(live_slot(index), flags, caps) : ERROR_DEVICE_NOT_CONNECTED;
}

// The game resolves XInput by name at start-up. Answering those lookups with our own functions
// puts us between the game and whatever supplies XInput (Windows or Steam Input) without
// depending on addresses inside one particular build of the executable.
static FARPROC WINAPI hk_get_proc_address(HMODULE module, LPCSTR name) {
    FARPROC real = o_get_proc_address(module, name);
    if (!real || !HIWORD((ULONG_PTR)name)) return real;
    if (!strcmp(name, "XInputGetState")) {
        r_get_state = (GetState_t)real;
        logf("XInputGetState routed through pop-pad");
        return (FARPROC)my_get_state;
    }
    if (!strcmp(name, "XInputSetState")) {
        r_set_state = (SetState_t)real;
        return (FARPROC)my_set_state;
    }
    if (!strcmp(name, "XInputGetCapabilities")) {
        r_get_capabilities = (GetCapabilities_t)real;
        return (FARPROC)my_get_capabilities;
    }
    return real;
}

void xi_install(HMODULE exe) {
    bool ok = iat_hook(exe, "KERNEL32.dll", "GetProcAddress", (void*)hk_get_proc_address, (void**)&o_get_proc_address);
    logf("GetProcAddress hook: %s", ok ? "installed" : "FAILED");
}
