// SPDX-License-Identifier: GPL-3.0-or-later
#include "pad_style.h"

static char lower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c; }

static bool starts_with_ci(const char* text, const char* word) {
    for (; *word; text++, word++)
        if (lower(*text) != *word) return false;
    return true;
}

static bool hex4(const char* text, unsigned* out) {
    unsigned value = 0;
    for (int i = 0; i < 4; i++) {
        char c = lower(text[i]);
        if (c >= '0' && c <= '9')
            value = value * 16 + (unsigned)(c - '0');
        else if (c >= 'a' && c <= 'f')
            value = value * 16 + (unsigned)(c - 'a' + 10);
        else
            return false;
    }
    *out = value;
    return true;
}

bool hid_path_ids(const char* path, unsigned* vendor, unsigned* product) {
    for (const char* p = path; *p; p++) {
        if (!starts_with_ci(p, "vid_")) continue;
        unsigned v, d;
        if (hex4(p + 4, &v) && starts_with_ci(p + 8, "&pid_") && hex4(p + 13, &d)) {
            *vendor = v;
            *product = d;
            return true;
        }
    }
    return false;
}

void pad_vendors_add(PadVendors* set, unsigned vendor) {
    switch (vendor) {
        case 0x054C: set->playstation = true; break;
        case 0x057E: set->nintendo = true; break;
        case 0x045E: set->microsoft = true; break;
        // Valve: Steam Input's virtual pad (and its own hardware). It copies the Xbox layout whatever
        // the real controller is, so it says nothing about which icons to draw.
        case 0x28DE: break;
        default: set->other = true; break;
    }
}

const char* pad_style_for(const PadVendors& set) {
    if (set.playstation) return "playstation";
    if (set.nintendo) return "switch";
    if (set.other) return "generic";
    return "xbox";
}
