#pragma once
#pragma warning(push, 0)
#include <windows.h>
#pragma warning(pop)

enum RenderPipeline : UINT
{
    RENDER_DEFAULT = 0, // standard UV mapping
    RENDER_TRIPLANAR,   // triplanar mapping
    RENDER_HEIGHTFIELD, // heightfield pipeline
    RENDER_COUNT
};

static const char* g_renderPipelineNames[RENDER_COUNT] = {
    "Default",
    "Triplanar",
    "Heightfield"
};