// SPDX-License-Identifier: GPL-3.0-or-later
// The one XInput type common.h names in a declaration; see windows.h.
#pragma once
#include "windows.h"

struct XINPUT_GAMEPAD {
    WORD wButtons;
    BYTE bLeftTrigger, bRightTrigger;
    int16_t sThumbLX, sThumbLY, sThumbRX, sThumbRY;
};
