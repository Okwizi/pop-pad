// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#define WIN32_LEAN_AND_MEAN
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <xinput.h>

struct Config {
    bool log;
    // Route XInput pads to the game's own Xbox controller code instead of its generic joystick path.
    bool native_xinput;
    // Give profiles that have no pad bindings the game's own default Xbox layout.
    bool add_default_bindings;
    // Percentage of stick travel treated as full deflection; 100 leaves the sticks untouched.
    int stick_saturation;
    // Show button icons instead of text labels for pad inputs.
    bool glyphs;
    // Development only: a scripted pad and a command file, so the game can be driven remotely.
    bool dev_virtual_pad;
    bool dev_commands;
    int dev_icon_test;
};

extern Config g_cfg;
extern char g_dir[MAX_PATH];

void log_init();
void logf(const char* fmt, ...);

bool iat_hook(HMODULE mod, const char* dll, const char* func, void* repl, void** orig);
void* vt_patch(void* obj, int index, void* repl);

void di_hook_interface(void* di8);
void xi_install(HMODULE exe);
void d3d_install(HMODULE exe);
void fs_install(HMODULE exe);
bool fs_read_whole(const char* path, BYTE* buffer, DWORD capacity, DWORD* size);
void cmd_start();
void profile_fix_all();

struct MftFont;
struct IDirect3DDevice9;
struct IDirect3DTexture9;
struct IDirect3DBaseTexture9;
bool font_redirect(const char* path, char* replaced, size_t replaced_size);
bool names_redirect(const char* path, char* replaced, size_t replaced_size);
void glyphs_font_opened(const MftFont* font, bool patched);
void glyphs_texture_uploaded(IDirect3DDevice9* device, IDirect3DBaseTexture9* source, IDirect3DBaseTexture9* destination);
void glyphs_device_reset();
IDirect3DBaseTexture9* glyphs_substitute(IDirect3DBaseTexture9* texture);

void vpad_set(const XINPUT_GAMEPAD& pad);
XINPUT_GAMEPAD vpad_get();

bool shot_request(const char* name, DWORD timeout_ms);
HWND game_window();
