#pragma once

#include <windows.h>

namespace RootParameters {
    constexpr UINT PER_DRAW_CONSTANTS = 0;
    constexpr UINT PER_FRAME_CBV = 1;
    constexpr UINT PER_SCENE_DESC_TABLE = 2;
    constexpr UINT SRV_DESC_TABLE = 3;
}