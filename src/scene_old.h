#pragma once
#pragma warning(push, 0)
#include <SDL3/SDL.h>
#include <windows.h>
#include <directx/d3d12.h>
#include <dxgi1_6.h>
#include <dxgi1_2.h>
#include <directx/d3dx12.h>
#include <DirectXTex.h>

#include <imgui.h>
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_dx12.h>
#include <ImGuizmo.h>

#include <cgltf.h>
#include <cJSON.h>
#pragma warning(pop)

#include "mesh_data.h"
// todo change this to a "scene" pattern
static const int g_scene_objects_count = 16;
static struct
{
    // todo: add ambient colour
    // add one skybox set
    struct
    {
        char nametag[128] = "Unamed";
        DirectX::XMFLOAT3 pos;
        DirectX::XMFLOAT4 rot = {0, 0, 0, 1}; // quaternion (default identity)
        DirectX::XMFLOAT3 scale = {1, 1, 1};  // default {1,1,1}
        PrimitiveType type = PrimitiveType::PRIMITIVE_CUBE;
    } objects[g_scene_objects_count];

    void write()
    {
        SDL_IOStream *file = SDL_IOFromFile("scene.bin", "wb");
        if (!file)
            return;
        SDL_WriteIO(file, objects, sizeof(objects));
        SDL_CloseIO(file);
    }

    void read()
    {
        size_t size;
        void *data = SDL_LoadFile("scene.bin", &size);
        if (data && size == sizeof(objects))
        {
            SDL_memcpy(objects, data, sizeof(objects));
        }
        SDL_free(data);
    }
} g_scene;