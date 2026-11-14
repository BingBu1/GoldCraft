// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// PF_lightstyle_I calls this before its original storage/broadcast. Other
// styles and ordinary-server writes retain their original path.
const char* GoldCraftLightStyleWrite(int style, const char* pattern);
