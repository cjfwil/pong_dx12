#pragma once
#include "mesh_data.h" 
#include "render_pipeline_data.h"

#define MAX_SCENE_OBJECTS 512

enum ObjectType : uint32_t
{
    OBJECT_PRIMITIVE = 0,
    OBJECT_HEIGHTFIELD,
    OBJECT_LOADED_MODEL,
    OBJECT_SKY_SPHERE,
    OBJECT_WATER,
    OBJECT_COUNT
};

static const char* g_objectTypeNames[OBJECT_COUNT] = {
    "Primitive",
    "Heightfield",
    "Loaded Model",
    "Sky",
    "Water"
};

struct SceneObject {
    char nametag[128];
    DirectX::XMFLOAT3 pos;
    DirectX::XMFLOAT4 rot;
    DirectX::XMFLOAT3 scale;
    ObjectType objectType;
    RenderPipeline pipeline;    
    union {
        struct {
            PrimitiveType primitiveType;
        } primitive;
        struct {
            char pathToHeightmap[256];
            uint32_t width; //example only, placeholder             
        } heightfield;
        struct {
            char pathTo[256]; //example only, placeholder 
            uint32_t model_index;
        } loaded_model;
        struct {            
            char pathToTexture[256]; //example only, placeholder             
        } sky_sphere;
        struct {
            float choppiness; //example only, placeholder (todo implement water)
        } water;
    } data;
};

struct Scene {
    SceneObject objects[MAX_SCENE_OBJECTS];
    int objectCount;
    // todo add more fields here later (ambient colour, lights etc.)
};