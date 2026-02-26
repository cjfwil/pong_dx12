//------------------------------------------------------------------------
// DESCRIPTOR LAYOUT – DO NOT EDIT
//   This file was automatically generated.
//   by meta_descriptors.py
//   Generated: 2026-02-26 03:23:00
//------------------------------------------------------------------------


#pragma once

// ----------------------------------------------------------------------------
// Constants (shared between C++ and HLSL)
// ----------------------------------------------------------------------------
#define MAX_HEIGHTMAP_TEXTURES 256
#define MAX_SKY_TEXTURES        16
#define MAX_LOADED_MODELS       64

#ifdef __cplusplus
// ----------------------------------------------------------------------------
// C++ specific: register bases and descriptor heap indices
// ----------------------------------------------------------------------------
namespace RegisterLayout {
    constexpr UINT HEIGHTMAP_REGISTER_BASE = 1;
    constexpr UINT HEIGHTMAP_COUNT          = MAX_HEIGHTMAP_TEXTURES;
    constexpr UINT SKY_REGISTER_BASE        = HEIGHTMAP_REGISTER_BASE + HEIGHTMAP_COUNT;
    constexpr UINT SKY_COUNT                 = MAX_SKY_TEXTURES;
    constexpr UINT ALBEDO_REGISTER_BASE      = SKY_REGISTER_BASE + SKY_COUNT;
    constexpr UINT ALBEDO_COUNT               = MAX_LOADED_MODELS;
}

namespace DescriptorIndices {
    constexpr UINT PER_FRAME_CBV_START = 0;
    constexpr UINT PER_SCENE_CBV       = 3;
    constexpr UINT TEXTURE_SRV          = 4;
    constexpr UINT HEIGHTMAP_SRV        = 5;
    constexpr UINT SKY_SRV              = 261;
    constexpr UINT MODEL_ALBEDO_SRV     = 277;
    constexpr UINT NUM_DESCRIPTORS      = 341;
}
#else // HLSL
// ----------------------------------------------------------------------------
// HLSL specific: register bases as preprocessor macros (literal values)
// ----------------------------------------------------------------------------
#define HEIGHTMAP_REGISTER_BASE t1
#define SKY_REGISTER_BASE       t257
#define ALBEDO_REGISTER_BASE    t273
#endif
