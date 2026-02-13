#pragma once
#include "mesh_data.h"   // for PrimitiveType

#define MAX_SCENE_OBJECTS 16

typedef struct {
    char nametag[128];
    DirectX::XMFLOAT3 pos;
    DirectX::XMFLOAT4 rot;
    DirectX::XMFLOAT3 scale;
    PrimitiveType type;
} SceneObject;

typedef struct {
    SceneObject objects[MAX_SCENE_OBJECTS];
    int objectCount;
    // todo add more fields here later (ambient colour, etc.)
} Scene;