// SPDX-License-Identifier: GPL-3.0-or-later
// What Windows says is plugged in, asked in a way the game-side input filters do not answer. Under
// Steam Input the game is shown one virtual Xbox pad and nothing else, so the real controller's
// maker has to come from the system's device list instead.
#include "common.h"
#include "pad_style.h"
// Older MinGW-w64 headers, such as the CI runner's, do not declare the HID functions extern "C", so
// the linker looks for C++ names that hid.dll does not have. hidsdi.h has to come before hidpi.h.
extern "C" {
#include <hidsdi.h>
#include <hidpi.h>
}
#include <setupapi.h>

// The interface Windows' Xbox 360 class driver exposes for every XInput pad, whatever its maker.
static const GUID GUID_XUSB = {0xEC87F1E3, 0xC13B, 0x4100, {0xB5, 0xF7, 0x8B, 0x84, 0xD5, 0x42, 0x60, 0xCB}};

static void found(PadVendors* set, const char* kind, unsigned vendor, unsigned product) {
    logf("controller present: %04X:%04X (%s)", vendor, product, kind);
    pad_vendors_add(set, vendor);
}

static bool next_interface(HDEVINFO devices, const GUID& guid, DWORD index, char* path, DWORD capacity) {
    SP_DEVICE_INTERFACE_DATA iface;
    iface.cbSize = sizeof iface;
    if (!SetupDiEnumDeviceInterfaces(devices, nullptr, &guid, index, &iface)) return false;
    BYTE buffer[1024];
    SP_DEVICE_INTERFACE_DETAIL_DATA_A* detail = (SP_DEVICE_INTERFACE_DETAIL_DATA_A*)buffer;
    detail->cbSize = sizeof *detail;
    path[0] = 0;
    if (SetupDiGetDeviceInterfaceDetailA(devices, &iface, detail, sizeof buffer, nullptr, nullptr))
        lstrcpynA(path, detail->DevicePath, (int)capacity);
    return true;
}

// Opening with no access rights is enough to read a HID device's identity and usage, and works
// on a pad another program holds open.
static bool is_gamepad(const char* path, unsigned* vendor, unsigned* product) {
    HANDLE file = CreateFileA(path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    bool pad = false;
    HIDD_ATTRIBUTES attributes;
    attributes.Size = sizeof attributes;
    PHIDP_PREPARSED_DATA data = nullptr;
    if (HidD_GetAttributes(file, &attributes) && HidD_GetPreparsedData(file, &data)) {
        HIDP_CAPS caps;
        // Usage page 1 (generic desktop): 4 is a joystick, 5 a gamepad.
        if (HidP_GetCaps(data, &caps) == HIDP_STATUS_SUCCESS && caps.UsagePage == 1 && (caps.Usage == 4 || caps.Usage == 5)) {
            *vendor = attributes.VendorID;
            *product = attributes.ProductID;
            pad = true;
        }
    }
    if (data) HidD_FreePreparsedData(data);
    CloseHandle(file);
    return pad;
}

static void scan_hid(PadVendors* set) {
    GUID hid;
    HidD_GetHidGuid(&hid);
    HDEVINFO devices = SetupDiGetClassDevsA(&hid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (devices == INVALID_HANDLE_VALUE) return;
    char path[MAX_PATH];
    unsigned vendor, product;
    for (DWORD i = 0; next_interface(devices, hid, i, path, sizeof path); i++)
        if (path[0] && is_gamepad(path, &vendor, &product)) found(set, "hid", vendor, product);
    SetupDiDestroyDeviceInfoList(devices);
}

static void scan_xusb(PadVendors* set) {
    HDEVINFO devices = SetupDiGetClassDevsA(&GUID_XUSB, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (devices == INVALID_HANDLE_VALUE) return;
    char path[MAX_PATH];
    unsigned vendor, product;
    for (DWORD i = 0; next_interface(devices, GUID_XUSB, i, path, sizeof path); i++)
        if (hid_path_ids(path, &vendor, &product)) found(set, "xinput", vendor, product);
    SetupDiDestroyDeviceInfoList(devices);
}

PadVendors pad_scan() {
    PadVendors set = {};
    scan_hid(&set);
    scan_xusb(&set);
    return set;
}
