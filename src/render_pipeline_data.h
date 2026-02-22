#pragma once
#pragma warning(push, 0)
#include <windows.h>
#pragma warning(pop)

enum RenderPipeline : UINT
{
    RENDER_DEFAULT = 0, // standard UV mapping
    RENDER_TRIPLANAR,   // triplanar mapping
    RENDER_HEIGHTFIELD, // heightfield pipeline
    RENDER_SKY,
    RENDER_COUNT
};

enum BlendMode : UINT {
    BLEND_OPAQUE = 0,
    BLEND_ALPHA,
    BLEND_COUNT
};

static const char* g_renderPipelineNames[RenderPipeline::RENDER_COUNT] = {
    "Default",
    "Triplanar",
    "Heightfield",
    "Sky"
};