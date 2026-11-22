// SPDX-License-Identifier: GPL-3.0-or-later
// Which makers' controllers are plugged in, and the icon set that fits them. No Windows types
// here, so the unit tests can build it.
#pragma once

struct PadVendors {
    bool playstation, nintendo, microsoft, other;
};

// Reads "vid_XXXX&pid_YYYY", in either case, out of a Windows device path.
bool hid_path_ids(const char* path, unsigned* vendor, unsigned* product);

void pad_vendors_add(PadVendors* set, unsigned vendor);

// "playstation", "switch", "generic" or "xbox"; see the README for the rules.
const char* pad_style_for(const PadVendors& set);

// The controllers Windows reports as present (implemented in hid_scan.cpp).
PadVendors pad_scan();
