#pragma once
#include "mesh_data.h" 
#include "render_pipeline_data.h"

#define MAX_SCENE_OBJECTS 32

typedef struct {
    char nametag[128];
    DirectX::XMFLOAT3 pos;
    DirectX::XMFLOAT4 rot;
    DirectX::XMFLOAT3 scale;
    PrimitiveType type;
    RenderPipeline pipeline;
} SceneObject;

typedef struct {
    SceneObject objects[MAX_SCENE_OBJECTS];
    int objectCount;
    // todo add more fields here later (ambient colour, lights etc.)
} Scene;