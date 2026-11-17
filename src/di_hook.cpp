// SPDX-License-Identifier: GPL-3.0-or-later
#include "common.h"
#include <dinput.h>
#include <cstring>

// The game hands a pad to its XInput code only when DirectInput reports this exact hardware ID
// (a wired Xbox 360 pad). Every other XInput pad falls through to its generic joystick path,
// where both triggers share one axis and nothing is bound. So the real XInput-class devices are
// hidden from the game and one stand-in device with the accepted ID is listed in their place;
// the game then reads the pad through XInput, which is also what Steam Input feeds.
static const DWORD XBOX360_VIDPID = MAKELONG(0x045E, 0x028E);
static const GUID XBOX360_PRODUCT = {XBOX360_VIDPID, 0, 0, {0, 0, 'P', 'I', 'D', 'V', 'I', 'D'}};
static const GUID STANDIN_INSTANCE = {0x5A0D0001, 0x7070, 0x4144, {0x80, 0x00, 'P', 'o', 'p', 'P', 'a', 'd'}};

enum { PROP_JOYSTICKID = 15, PROP_VIDPID = 24 };
enum { VT_CREATE_DEVICE = 3, VT_ENUM_DEVICES = 4 };

static const DWORD STANDIN_DEVTYPE = DI8DEVTYPE_GAMEPAD | (DI8DEVTYPEGAMEPAD_STANDARD << 8) | 0x10000;

static void fill_standin_instance(DIDEVICEINSTANCEA* out) {
    DWORD size = out->dwSize;
    memset(out, 0, size);
    out->dwSize = size;
    out->guidInstance = STANDIN_INSTANCE;
    out->guidProduct = XBOX360_PRODUCT;
    out->dwDevType = STANDIN_DEVTYPE;
    lstrcpyA(out->tszInstanceName, "XInput controller (pop-pad)");
    lstrcpyA(out->tszProductName, "XInput controller (pop-pad)");
    out->wUsagePage = 1;
    out->wUsage = 5;
}

// The game only initialises this device, reads its hardware ID, then drops it in favour of
// XInput, so nothing here has to report input.
struct StandInPad final : IDirectInputDevice8A {
    LONG refs = 1;

    STDMETHODIMP QueryInterface(REFIID, LPVOID* out) override {
        *out = this;
        AddRef();
        return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&refs); }
    STDMETHODIMP_(ULONG) Release() override {
        LONG n = InterlockedDecrement(&refs);
        if (n == 0) delete this;
        return n;
    }
    STDMETHODIMP GetCapabilities(LPDIDEVCAPS caps) override {
        DWORD size = caps->dwSize;
        memset(caps, 0, size);
        caps->dwSize = size;
        caps->dwFlags = DIDC_ATTACHED | DIDC_EMULATED;
        caps->dwDevType = STANDIN_DEVTYPE;
        caps->dwAxes = 5;
        caps->dwButtons = 10;
        caps->dwPOVs = 1;
        return DI_OK;
    }
    STDMETHODIMP EnumObjects(LPDIENUMDEVICEOBJECTSCALLBACKA, LPVOID, DWORD) override { return DI_OK; }
    STDMETHODIMP GetProperty(REFGUID prop, LPDIPROPHEADER header) override {
        ULONG_PTR id = (ULONG_PTR)&prop;
        if (id == PROP_VIDPID) {
            ((DIPROPDWORD*)header)->dwData = XBOX360_VIDPID;
            return DI_OK;
        }
        // The game uses this as the XInput user index; xi_hook maps index 0 to whichever slot is live.
        if (id == PROP_JOYSTICKID) {
            ((DIPROPDWORD*)header)->dwData = 0;
            return DI_OK;
        }
        return DIERR_UNSUPPORTED;
    }
    STDMETHODIMP SetProperty(REFGUID, LPCDIPROPHEADER) override { return DI_OK; }
    STDMETHODIMP Acquire() override { return DI_OK; }
    STDMETHODIMP Unacquire() override { return DI_OK; }
    STDMETHODIMP GetDeviceState(DWORD size, LPVOID data) override {
        memset(data, 0, size);
        return DI_OK;
    }
    STDMETHODIMP GetDeviceData(DWORD, LPDIDEVICEOBJECTDATA, LPDWORD count, DWORD) override {
        if (count) *count = 0;
        return DI_OK;
    }
    STDMETHODIMP SetDataFormat(LPCDIDATAFORMAT) override { return DI_OK; }
    STDMETHODIMP SetEventNotification(HANDLE) override { return DI_OK; }
    STDMETHODIMP SetCooperativeLevel(HWND, DWORD) override { return DI_OK; }
    STDMETHODIMP GetObjectInfo(LPDIDEVICEOBJECTINSTANCEA, DWORD, DWORD) override { return DIERR_OBJECTNOTFOUND; }
    STDMETHODIMP GetDeviceInfo(LPDIDEVICEINSTANCEA info) override {
        fill_standin_instance(info);
        return DI_OK;
    }
    STDMETHODIMP RunControlPanel(HWND, DWORD) override { return DI_OK; }
    STDMETHODIMP Initialize(HINSTANCE, DWORD, REFGUID) override { return DI_OK; }
    STDMETHODIMP CreateEffect(REFGUID, LPCDIEFFECT, LPDIRECTINPUTEFFECT*, LPUNKNOWN) override { return DIERR_UNSUPPORTED; }
    STDMETHODIMP EnumEffects(LPDIENUMEFFECTSCALLBACKA, LPVOID, DWORD) override { return DI_OK; }
    STDMETHODIMP GetEffectInfo(LPDIEFFECTINFOA, REFGUID) override { return DIERR_UNSUPPORTED; }
    STDMETHODIMP GetForceFeedbackState(LPDWORD) override { return DIERR_UNSUPPORTED; }
    STDMETHODIMP SendForceFeedbackCommand(DWORD) override { return DIERR_UNSUPPORTED; }
    STDMETHODIMP EnumCreatedEffectObjects(LPDIENUMCREATEDEFFECTOBJECTSCALLBACK, LPVOID, DWORD) override { return DI_OK; }
    STDMETHODIMP Escape(LPDIEFFESCAPE) override { return DIERR_UNSUPPORTED; }
    STDMETHODIMP Poll() override { return DI_OK; }
    STDMETHODIMP SendDeviceData(DWORD, LPCDIDEVICEOBJECTDATA, LPDWORD, DWORD) override { return DIERR_UNSUPPORTED; }
    STDMETHODIMP EnumEffectsInFile(LPCSTR, LPDIENUMEFFECTSINFILECALLBACK, LPVOID, DWORD) override { return DI_OK; }
    STDMETHODIMP WriteEffectToFile(LPCSTR, DWORD, LPDIFILEEFFECT, DWORD) override { return DIERR_UNSUPPORTED; }
    STDMETHODIMP BuildActionMap(LPDIACTIONFORMATA, LPCSTR, DWORD) override { return DIERR_UNSUPPORTED; }
    STDMETHODIMP SetActionMap(LPDIACTIONFORMATA, LPCSTR, DWORD) override { return DIERR_UNSUPPORTED; }
    STDMETHODIMP GetImageInfo(LPDIDEVICEIMAGEINFOHEADERA) override { return DIERR_UNSUPPORTED; }
};

typedef HRESULT(STDMETHODCALLTYPE* EnumDevices_t)(IDirectInput8A*, DWORD, LPDIENUMDEVICESCALLBACKA, LPVOID, DWORD);
typedef HRESULT(STDMETHODCALLTYPE* CreateDevice_t)(IDirectInput8A*, REFGUID, LPDIRECTINPUTDEVICE8A*, LPUNKNOWN);

static EnumDevices_t o_enum_devices;
static CreateDevice_t o_create_device;

// XInput-class pads expose their HID interface with "IG_" in the device path; that marker is
// Microsoft's documented way to tell them apart from plain DirectInput devices.
static int xinput_hardware_ids(DWORD* ids, int capacity) {
    UINT count = 0;
    if (GetRawInputDeviceList(nullptr, &count, sizeof(RAWINPUTDEVICELIST)) != 0 || count == 0) return 0;
    RAWINPUTDEVICELIST* list = (RAWINPUTDEVICELIST*)HeapAlloc(GetProcessHeap(), 0, count * sizeof(RAWINPUTDEVICELIST));
    if (!list) return 0;
    int found = 0;
    UINT got = GetRawInputDeviceList(list, &count, sizeof(RAWINPUTDEVICELIST));
    for (UINT i = 0; got != (UINT)-1 && i < got && found < capacity; i++) {
        if (list[i].dwType != RIM_TYPEHID) continue;
        char name[512];
        UINT name_size = sizeof name;
        if ((int)GetRawInputDeviceInfoA(list[i].hDevice, RIDI_DEVICENAME, name, &name_size) <= 0) continue;
        CharUpperA(name);
        if (!strstr(name, "IG_")) continue;
        RID_DEVICE_INFO info;
        info.cbSize = sizeof info;
        UINT info_size = sizeof info;
        if ((int)GetRawInputDeviceInfoA(list[i].hDevice, RIDI_DEVICEINFO, &info, &info_size) <= 0) continue;
        ids[found++] = MAKELONG(info.hid.dwVendorId, info.hid.dwProductId);
    }
    HeapFree(GetProcessHeap(), 0, list);
    return found;
}

struct EnumContext {
    LPDIENUMDEVICESCALLBACKA callback;
    LPVOID ref;
    bool stopped;
    DWORD xinput_ids[16];
    int xinput_id_count;
};

static BOOL CALLBACK enum_relay(LPCDIDEVICEINSTANCEA instance, LPVOID param) {
    EnumContext* ctx = (EnumContext*)param;
    bool xinput = false;
    for (int i = 0; i < ctx->xinput_id_count; i++) xinput |= ctx->xinput_ids[i] == instance->guidProduct.Data1;
    bool hide = xinput && g_cfg.native_xinput;
    logf("enum: '%s' product=%08lX type=%08lX xinput=%d%s", instance->tszProductName, instance->guidProduct.Data1, instance->dwDevType,
         xinput, hide ? " (hidden from the game)" : "");
    if (hide) return DIENUM_CONTINUE;
    BOOL result = ctx->callback(instance, ctx->ref);
    if (result == DIENUM_STOP) ctx->stopped = true;
    return result;
}

static HRESULT STDMETHODCALLTYPE hk_enum_devices(IDirectInput8A* self, DWORD type, LPDIENUMDEVICESCALLBACKA callback, LPVOID ref,
                                                 DWORD flags) {
    logf("EnumDevices type=%lu flags=%lx", type, flags);
    if (!callback) return o_enum_devices(self, type, callback, ref, flags);

    EnumContext ctx = {};
    ctx.callback = callback;
    ctx.ref = ref;
    ctx.xinput_id_count = xinput_hardware_ids(ctx.xinput_ids, 16);
    HRESULT hr = o_enum_devices(self, type, enum_relay, &ctx, flags);

    bool pads = type == DI8DEVCLASS_GAMECTRL || type == DI8DEVCLASS_ALL || type == DI8DEVTYPE_GAMEPAD || type == DI8DEVTYPE_JOYSTICK;
    if (SUCCEEDED(hr) && !ctx.stopped && pads && g_cfg.native_xinput) {
        // Listed whether or not a pad is connected right now: the game enumerates once at
        // start-up, and its XInput code copes with an empty slot, so a pad that wakes later works.
        DIDEVICEINSTANCEA standin;
        standin.dwSize = sizeof standin;
        fill_standin_instance(&standin);
        logf("enum: listing the stand-in XInput device");
        callback(&standin, ref);
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE hk_create_device(IDirectInput8A* self, REFGUID guid, LPDIRECTINPUTDEVICE8A* out, LPUNKNOWN outer) {
    if (g_cfg.native_xinput && out && IsEqualGUID(guid, STANDIN_INSTANCE)) {
        *out = new StandInPad();
        logf("CreateDevice: stand-in XInput device");
        return DI_OK;
    }
    return o_create_device(self, guid, out, outer);
}

void di_hook_interface(void* di8) {
    void* previous = vt_patch(di8, VT_CREATE_DEVICE, (void*)hk_create_device);
    if (previous) o_create_device = (CreateDevice_t)previous;
    previous = vt_patch(di8, VT_ENUM_DEVICES, (void*)hk_enum_devices);
    if (previous) o_enum_devices = (EnumDevices_t)previous;
    logf("DirectInput interface %p hooked", di8);
}
