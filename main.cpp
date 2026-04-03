#pragma warning(disable : 5045) // disabling the spectre mitigation warning (not relevant because we are a game, no sensitive information should be in this program)
#pragma warning(disable : 4238) // nonstandard lvalue as rvalue warning
#pragma warning(disable : 4820) // padding warnings
#pragma warning(disable : 4061) // unhandled enum in switch warning
#pragma comment(lib, "SDL3.lib")
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "dxcompiler.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "imgui.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "ole32.lib")

#if defined(_DEBUG)
#pragma comment(lib, "DirectXTex.lib")
#pragma comment(lib, "debug/ImGuizmo.lib")
#pragma comment(lib, "debug/cJSON.lib")
#else
#pragma comment(lib, "DirectXTex_release.lib")
#pragma comment(lib, "release/ImGuizmo.lib")
#pragma comment(lib, "release/cJSON.lib")
#endif

#pragma warning(push, 0)
#include <SDL3/SDL.h>
#include <windows.h>
#include <directx/d3d12.h>
#include <dxgi1_6.h>
#include <dxgi1_2.h>
#include <directx/d3dx12.h>
#include <DirectXTex.h>
#include <DirectXMath.h>

#include <imgui.h>
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_dx12.h>
#include <ImGuizmo.h>

#define CGLTF_IMPLEMENTATION
#include <cgltf.h>
#include <cJSON.h>
#pragma warning(pop)

#include "local_error.h"
#include "config_ini_io.cpp"
#include "renderer_dx12.cpp"
#include "scene_data.h"
#include "generated/scene_json.cpp"
#include "ray_intersections.h"
#include "cylinder_overlap.h"

static bool g_show_player_wireframe = false;
static struct
{
    bool valid = false;
    DirectX::XMFLOAT3 start;
    DirectX::XMFLOAT3 end;
    bool hit;
    DirectX::XMFLOAT3 hitPoint;
} g_debugRay;

static ConfigData g_liveConfigData = {};
static Scene g_scene; // TODO: this is called scene but in practice it is only the static geometry, perhaps we should extend when we add enemies, or we should rename to static environment geometry only? TODO decide

enum PatrolMode
{
    PATROL_WRAP,              // wraps around to index 0, so a bot with 3 patrol points will travel in a triangle pattern
    PATROL_REVERSE_DIRECTION, // goes in backwards order once reaches the end of the patrol points, so bot will travel in a curve always
};

// definition: a "bot object" is any object that is autonomously and dynamically controlled by the computer to make decisions about movement and actions and can be interacted with by player,
// positive examples: enemies, NPCs, animals that flee in reactio to player movement/attacks
// negative examples: very distant birds (visual only), doors (different type, environment interactable)
static const int maxPatrolPoints = 16;

enum BotState
{
    BOT_ALIVE,
    BOT_DYING,
    BOT_DEAD
};

struct BotObjects
{
    bool visible;
    DirectX::XMFLOAT3 pos;
    DirectX::XMFLOAT4 rot;
    DirectX::XMFLOAT3 scale;
    // TODO: add shape/model (for now every bot is a sphere)

    // game data
    BotState state = BOT_ALIVE;

    // patrol data (TODO: abstract this when we have different behaviours (maybe union with other behaviour data))
    DirectX::XMFLOAT3 patrolPoints[maxPatrolPoints];
    int patrolPointCount;
    PatrolMode patrolMode = PATROL_WRAP;
    int patrolDirection = 1; // only for PATROL_REVERSE_DIRECTION

    float speed = 4.0f;
    int currentPatrolPointTargetIndex = 0;

    bool isWaiting = false;
    float waitTimeRemaining = 0.0f;
    float waitTimeSeconds = 2.0f;

    // realistic movement values
    DirectX::XMFLOAT3 velocity;
    float maxSpeed = 10.0f;
    float acceleration = 8.0f;
};

#define MAX_BOT_OBJECTS 64
static BotObjects g_bot_objects[MAX_BOT_OBJECTS] = {}; // TODO: This structure is runtime only, this data is store on file, perhaps the scene.json

void UpdateBots(float deltaTime)
{
    const float EPSILON = 0.1f;

    for (int i = 0; i < MAX_BOT_OBJECTS; ++i)
    {
        BotObjects &bot = g_bot_objects[i];

        // ---- STATE HANDLING ----
        if (bot.state == BOT_DYING)
        {
            const float GRAVITY = 15.0f;  // units per second squared
            const float GROUND_Y = 10.0f; // constant landing height

            // Apply gravity to vertical velocity
            bot.velocity.y -= GRAVITY * deltaTime;

            // Update position using current velocity
            bot.pos.x += bot.velocity.x * deltaTime;
            bot.pos.y += bot.velocity.y * deltaTime;
            bot.pos.z += bot.velocity.z * deltaTime;

            // When we hit the ground, stop all movement and become DEAD
            if (bot.pos.y <= GROUND_Y)
            {
                bot.pos.y = GROUND_Y;
                bot.velocity = {0, 0, 0};
                bot.state = BOT_DEAD;
            }
            continue; // skip ALIVE logic
        }
        else if (bot.state == BOT_DEAD)
        {
            continue; // do nothing
        }

        // Guard: ensure patrolPointCount is valid
        if (bot.patrolPointCount <= 0)
            continue; // no patrol points – bot does nothing
        if (bot.patrolPointCount > maxPatrolPoints)
            bot.patrolPointCount = maxPatrolPoints; // clamp to array size

        // Guard: ensure current target index is within bounds
        if (bot.currentPatrolPointTargetIndex < 0 || bot.currentPatrolPointTargetIndex >= bot.patrolPointCount)
            bot.currentPatrolPointTargetIndex = 0;

        if (bot.isWaiting)
        {
            bot.waitTimeRemaining -= deltaTime;
            if (bot.waitTimeRemaining <= 0.0f)
            {
                bot.isWaiting = false;

                if (bot.patrolMode == PATROL_WRAP)
                {
                    // simple wrap
                    bot.currentPatrolPointTargetIndex = (bot.currentPatrolPointTargetIndex + 1) % bot.patrolPointCount;
                }
                else if (bot.patrolMode == PATROL_REVERSE_DIRECTION)
                {
                    int next = bot.currentPatrolPointTargetIndex + bot.patrolDirection;
                    if (next < 0 || next >= bot.patrolPointCount)
                    {
                        // reverse direction
                        bot.patrolDirection = -bot.patrolDirection;
                        next = bot.currentPatrolPointTargetIndex + bot.patrolDirection;
                    }
                    bot.currentPatrolPointTargetIndex = next;
                }
            }
            continue;
        }

        // Moving toward the current target using velocity control
        DirectX::XMFLOAT3 target = bot.patrolPoints[bot.currentPatrolPointTargetIndex];

        float dx = target.x - bot.pos.x;
        float dy = target.y - bot.pos.y;
        float dz = target.z - bot.pos.z;
        float distance = sqrtf(dx * dx + dy * dy + dz * dz);

        if (distance < EPSILON && bot.velocity.x == 0 && bot.velocity.y == 0 && bot.velocity.z == 0)
        {
            // Already at target and stationary
            bot.pos = target;
            bot.isWaiting = true;
            bot.waitTimeRemaining = bot.waitTimeSeconds;
            continue;
        }

        if (distance < EPSILON)
        {
            // Very close - snap and stop
            bot.pos = target;
            bot.velocity = {0, 0, 0};
            bot.isWaiting = true;
            bot.waitTimeRemaining = bot.waitTimeSeconds;
            continue;
        }

        // Compute desired speed based on distance (proportional control)
        float desired_speed = bot.maxSpeed;
        float r_stop = (bot.maxSpeed * bot.maxSpeed) / (2.0f * bot.acceleration);
        if (distance < r_stop)
        {
            desired_speed = (2.0f * bot.acceleration / bot.maxSpeed) * distance;
        }

        // Desired velocity direction
        float invDist = 1.0f / distance;
        float desiredVX = dx * invDist * desired_speed;
        float desiredVY = dy * invDist * desired_speed;
        float desiredVZ = dz * invDist * desired_speed;

        // Accelerate current velocity toward desired
        float dvx = desiredVX - bot.velocity.x;
        float dvy = desiredVY - bot.velocity.y;
        float dvz = desiredVZ - bot.velocity.z;
        float dvMag = sqrtf(dvx * dvx + dvy * dvy + dvz * dvz);
        float maxDelta = bot.acceleration * deltaTime;
        if (dvMag > maxDelta)
        {
            dvx = dvx / dvMag * maxDelta;
            dvy = dvy / dvMag * maxDelta;
            dvz = dvz / dvMag * maxDelta;
        }
        bot.velocity.x += dvx;
        bot.velocity.y += dvy;
        bot.velocity.z += dvz;

        // Update position
        bot.pos.x += bot.velocity.x * deltaTime;
        bot.pos.y += bot.velocity.y * deltaTime;
        bot.pos.z += bot.velocity.z * deltaTime;
    }
}

static float g_stepHeight = 1.0f; // put in g_player_bounds????/
static struct
{
    float height = 1.85f;
    float eyeHeight = 1.7f;
    float radius = 0.15f;
} g_player_bounds;

struct HeightmapDataCPU
{
    UINT8 *data;
    UINT width;
    UINT height;
};

static HeightmapDataCPU g_heightmapDataCPU = {};

void write_scene()
{
    char *json = scene_to_json(&g_scene);
    if (!json)
        return;

    SDL_IOStream *file = SDL_IOFromFile("scene.json", "wb");
    if (file)
    {
        SDL_WriteIO(file, json, SDL_strlen(json));
        SDL_CloseIO(file);
    }

    cJSON_free(json); // cJSON provides its own free function
}

void read_scene()
{
    size_t size;
    void *data = SDL_LoadFile("scene.json", &size);
    if (!data)
        return;

    scene_from_json((const char *)data, &g_scene);
    SDL_free(data);
}

static struct
{
    ID3D12DescriptorHeap *Heap = nullptr;
    D3D12_DESCRIPTOR_HEAP_TYPE HeapType = D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES;
    D3D12_CPU_DESCRIPTOR_HANDLE HeapStartCpu;
    D3D12_GPU_DESCRIPTOR_HANDLE HeapStartGpu;
    UINT HeapHandleIncrement;
    ImVector<int> FreeIndices;

    void Create(ID3D12Device *device, ID3D12DescriptorHeap *heap)
    {
        IM_ASSERT(Heap == nullptr && FreeIndices.empty());
        Heap = heap;
        D3D12_DESCRIPTOR_HEAP_DESC desc = heap->GetDesc();
        HeapType = desc.Type;
        HeapStartCpu = Heap->GetCPUDescriptorHandleForHeapStart();
        HeapStartGpu = Heap->GetGPUDescriptorHandleForHeapStart();
        HeapHandleIncrement = device->GetDescriptorHandleIncrementSize(HeapType);
        FreeIndices.reserve((int)desc.NumDescriptors);
        for (UINT n = desc.NumDescriptors; n > 0; n--)
            FreeIndices.push_back((const int)(n - 1));
    }
    void Destroy()
    {
        Heap = nullptr;
        FreeIndices.clear();
    }
    void Alloc(D3D12_CPU_DESCRIPTOR_HANDLE *out_cpu_desc_handle, D3D12_GPU_DESCRIPTOR_HANDLE *out_gpu_desc_handle)
    {
        IM_ASSERT(FreeIndices.Size > 0);
        int idx = FreeIndices.back();
        FreeIndices.pop_back();
        out_cpu_desc_handle->ptr = HeapStartCpu.ptr + (idx * HeapHandleIncrement);
        out_gpu_desc_handle->ptr = HeapStartGpu.ptr + (idx * HeapHandleIncrement);
    }
    void Free(D3D12_CPU_DESCRIPTOR_HANDLE out_cpu_desc_handle, D3D12_GPU_DESCRIPTOR_HANDLE out_gpu_desc_handle)
    {
        int cpu_idx = (int)((out_cpu_desc_handle.ptr - HeapStartCpu.ptr) / HeapHandleIncrement);
        int gpu_idx = (int)((out_gpu_desc_handle.ptr - HeapStartGpu.ptr) / HeapHandleIncrement);
        IM_ASSERT(cpu_idx == gpu_idx);
        FreeIndices.push_back(cpu_idx);
    }
} g_imguiHeap;

struct timing_state
{
    Uint64 lastCounter = 0;
    Uint64 ticksElapsed = 0;
    double upTime = 0.0;
    double deltaTime = 0.0;

    void InitTimer()
    {
        lastCounter = SDL_GetPerformanceCounter();
    }

    void UpdateTimer()
    {
        Uint64 now = SDL_GetPerformanceCounter();
        deltaTime = (double)(now - lastCounter) / (double)SDL_GetPerformanceFrequency();
        lastCounter = now;

        upTime += deltaTime;
        ticksElapsed++;
    }
};

enum struct WindowMode
{
    WINDOWED = 0,
    BORDERLESS = 1,
    NUM_WINDOW_MODES = 2
};

static struct
{
    bool applyWindowRequest = false;
    int requestedWidth;
    int requestedHeight;
    WindowMode requestedMode;
} window_request;

struct window_state
{
    uint32_t m_windowWidth;
    uint32_t m_windowHeight;
    WindowMode m_currentMode;
    SDL_Window *window = nullptr;
    HWND hwnd = nullptr;
    char *windowName = "window_name_todo_change";

    HWND GetWindowHWND()
    {
        if (!hwnd && window)
        {
            SDL_PropertiesID props = SDL_GetWindowProperties(window);
            hwnd = (HWND)SDL_GetPointerProperty(
                props,
                SDL_PROP_WINDOW_WIN32_HWND_POINTER,
                NULL);
            SDL_Log("SDL Retrieved HWND");
        }
        return hwnd;
    }

    inline Uint64 CalcWindowFlags(WindowMode mode)
    {
        Uint64 windowFlags = 0;
        if (mode == WindowMode::BORDERLESS)
            windowFlags |= SDL_WINDOW_BORDERLESS;
        return windowFlags;
    }

    void Create()
    {
        m_windowWidth = (uint32_t)g_liveConfigData.DisplaySettings.window_width;
        m_windowHeight = (uint32_t)g_liveConfigData.DisplaySettings.window_height;
        m_currentMode = (WindowMode)g_liveConfigData.DisplaySettings.window_mode;

        Uint64 windowFlags = CalcWindowFlags(m_currentMode);
        window = SDL_CreateWindow(windowName, (int)m_windowWidth, (int)m_windowHeight, windowFlags);
        if (window)
        {
            SDL_Log("SDL Window created.");
            GetWindowHWND();

            int w = 0, h = 0;
            SDL_GetWindowSize(window, &w, &h);
            g_engine.viewport_state.m_width = (UINT)w;
            g_engine.viewport_state.m_height = (UINT)h;
            g_engine.viewport_state.m_aspectRatio = (float)w / (float)h;
        }
        else
        {
            log_sdl_error("Couldn't create SDL window");
            HRAssert(E_UNEXPECTED);
        }
    }

    bool ApplyWindowMode()
    {
        WindowMode newMode = window_request.requestedMode;
        SDL_Log("Applying window mode: %d", newMode);

        SDL_DisplayID display = SDL_GetDisplayForWindow(window);
        SDL_Rect displayBounds;
        SDL_GetDisplayBounds(display, &displayBounds);

        SDL_SetWindowFullscreen(window, (newMode != WindowMode::WINDOWED));
        SDL_SetWindowResizable(window, false);
        SDL_SetWindowBordered(window, (newMode == WindowMode::WINDOWED));
        if (newMode == WindowMode::BORDERLESS)
        {
            SDL_SetWindowPosition(window, displayBounds.x, displayBounds.y);
            SDL_SetWindowSize(window, displayBounds.w, displayBounds.h);
        }
        else
        {
            SDL_SetWindowSize(window, (int)m_windowWidth, (int)m_windowHeight);
            SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
        }

        // Update viewport state
        int w, h;
        SDL_GetWindowSize(window, &w, &h);
        g_engine.viewport_state.m_width = (UINT)w;
        g_engine.viewport_state.m_height = (UINT)h;
        g_engine.viewport_state.m_aspectRatio = (float)w / (float)h;

        m_currentMode = newMode;

        SDL_Log("Window mode applied: %dx%d mode=%d", w, h, newMode);

        RecreateSwapChain();

        g_liveConfigData.DisplaySettings.window_width = w;
        g_liveConfigData.DisplaySettings.window_height = h;
        g_liveConfigData.DisplaySettings.window_mode = (int)m_currentMode;
        SaveConfig(&g_liveConfigData);

        return true;
    }
};

static struct
{
    bool mouseCaptured = false;
    bool lmbDown = false;
    bool keys[512] = {false};
} g_input;

struct FlyCamera
{
    DirectX::XMMATRIX viewMatrix;
    DirectX::XMMATRIX projectionMatrix;
    
    DirectX::XMVECTOR forward;
    DirectX::XMVECTOR right;
    DirectX::XMVECTOR up;
    DirectX::XMVECTOR eye;
    
    DirectX::XMFLOAT3 position = {0.2f, 24.3f, 5.071f};
    float yaw = 0.0f;
    float pitch = 0.0f;
    float moveSpeed = 5.0f;
    float lookSpeed = 0.002f;
    float padSpeed = 1.5f;    
    float fov_deg = 60.0f;

    void UpdateFlyCamera(float deltaTime)
    {
        forward = DirectX::XMVectorSet(
            sinf(yaw) * cosf(pitch),
            sinf(pitch),
            cosf(yaw) * cosf(pitch),
            0.0f);
        right = DirectX::XMVector3Cross(DirectX::XMVectorSet(0, 1, 0, 0), forward); // cross(up, forward)
        up = DirectX::XMVectorSet(0, 1, 0, 0);
        forward = DirectX::XMVector3Normalize(forward);
        right = DirectX::XMVector3Normalize(right);

        DirectX::XMVECTOR moveDelta = DirectX::XMVectorZero();
        if (g_input.keys[SDL_SCANCODE_W])
            moveDelta = DirectX::XMVectorAdd(moveDelta, forward);
        if (g_input.keys[SDL_SCANCODE_S])
            moveDelta = DirectX::XMVectorSubtract(moveDelta, forward);
        if (g_input.keys[SDL_SCANCODE_A])
            moveDelta = DirectX::XMVectorSubtract(moveDelta, right);
        if (g_input.keys[SDL_SCANCODE_D])
            moveDelta = DirectX::XMVectorAdd(moveDelta, right);
        if (g_input.keys[SDL_SCANCODE_Q])
            moveDelta = DirectX::XMVectorSubtract(moveDelta, up); // down
        if (g_input.keys[SDL_SCANCODE_E])
            moveDelta = DirectX::XMVectorAdd(moveDelta, up); // up

        // Apply movement scaled by deltaTime and speed
        if (!DirectX::XMVector3Equal(moveDelta, DirectX::XMVectorZero()))
        {
            moveDelta = DirectX::XMVector3Normalize(moveDelta);
            moveDelta = DirectX::XMVectorScale(moveDelta, deltaTime * moveSpeed);
            DirectX::XMVECTOR pos = XMLoadFloat3(&position);
            pos = DirectX::XMVectorAdd(pos, moveDelta);
            XMStoreFloat3(&position, pos);
        }

        eye = XMLoadFloat3(&g_camera.position);
    }

    void UpdateViewProjMatrices(float nearPlane, float farPlane)
    {
        DirectX::XMVECTOR at = DirectX::XMVectorAdd(g_camera.eye, g_camera.forward);
        viewMatrix = DirectX::XMMatrixLookAtLH(g_camera.eye, at, g_camera.up);
        projectionMatrix = DirectX::XMMatrixPerspectiveFovLH(
            DirectX::XMConvertToRadians(fov_deg),
            g_engine.viewport_state.m_aspectRatio,
            farPlane,
            nearPlane);
    }
} g_camera;

static bool g_view_editor = true;

// example for next pattern

#define MAX_SKY_LAYER_OBJECTS 8
const int g_draw_list_element_total = MAX_SCENE_OBJECTS + MAX_BOT_OBJECTS + MAX_SKY_LAYER_OBJECTS;

static struct
{
    int drawAmount = g_draw_list_element_total; // this should not be greater than g_draw_list_element_total
    ObjectType objectTypes[g_draw_list_element_total] = {};
    PrimitiveType primitiveTypes[g_draw_list_element_total] = {};
    UINT loadedModelIndex[g_draw_list_element_total] = {};
    RenderPipeline pipelines[g_draw_list_element_total] = {};
    UINT heightmapIndices[g_draw_list_element_total] = {};
    struct
    {
        DirectX::XMFLOAT3 pos[g_draw_list_element_total];
        DirectX::XMFLOAT4 rot[g_draw_list_element_total];
        DirectX::XMFLOAT3 scale[g_draw_list_element_total];
    } transforms;
} g_draw_list;

static struct
{
    timing_state timing;
    window_state window;

    bool isRunning = true;
} program_state;

bool PopulateCommandList()
{
    g_engine.pipeline_dx12.ResetCommandObjects(g_engine.sync_state, g_engine.msaa_state);

    g_engine.pipeline_dx12.m_commandList[g_engine.sync_state.m_frameIndex]->SetGraphicsRootSignature(g_engine.pipeline_dx12.m_rootSignature);

    ID3D12DescriptorHeap *ppHeaps[] = {g_engine.pipeline_dx12.m_mainHeap};
    g_engine.pipeline_dx12.m_commandList[g_engine.sync_state.m_frameIndex]->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);

    D3D12_GPU_VIRTUAL_ADDRESS cbvAddress = g_engine.graphics_resources.m_PerFrameConstantBuffer[g_engine.sync_state.m_frameIndex]->GetGPUVirtualAddress();
    g_engine.pipeline_dx12.m_commandList[g_engine.sync_state.m_frameIndex]->SetGraphicsRootConstantBufferView(1, cbvAddress);

    // Set per - scene CBV(root parameter 2 - descriptor table)
    UINT descriptorSize = g_engine.pipeline_dx12.m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    CD3DX12_GPU_DESCRIPTOR_HANDLE perSceneCbvHandle(
        g_engine.pipeline_dx12.m_mainHeap->GetGPUDescriptorHandleForHeapStart(),
        DescriptorIndices::PER_SCENE_CBV, // Per-scene CBV is after all per-frame CBVs
        descriptorSize);
    g_engine.pipeline_dx12.m_commandList[g_engine.sync_state.m_frameIndex]->SetGraphicsRootDescriptorTable(2, perSceneCbvHandle);

    // texture handle
    CD3DX12_GPU_DESCRIPTOR_HANDLE srvHandle(
        g_engine.pipeline_dx12.m_mainHeap->GetGPUDescriptorHandleForHeapStart(),
        DescriptorIndices::TEXTURE_SRV, // SRV is after all CBVs
        descriptorSize);
    g_engine.pipeline_dx12.m_commandList[g_engine.sync_state.m_frameIndex]->SetGraphicsRootDescriptorTable(3, srvHandle);

    g_engine.pipeline_dx12.m_commandList[g_engine.sync_state.m_frameIndex]->RSSetViewports(1, &g_engine.pipeline_dx12.m_viewport);
    g_engine.pipeline_dx12.m_commandList[g_engine.sync_state.m_frameIndex]->RSSetScissorRects(1, &g_engine.pipeline_dx12.m_scissorRect);

    // Choose RTV and DSV based on MSAA state
    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle;
    CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle;
    ID3D12Resource *renderTarget = nullptr;

    if (g_engine.msaa_state.m_enabled)
    {
        // MSAA path
        rtvHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(
            g_engine.pipeline_dx12.m_msaaRtvHeap->GetCPUDescriptorHandleForHeapStart(),
            (INT)g_engine.sync_state.m_frameIndex,
            g_engine.pipeline_dx12.m_rtvDescriptorSize);
        dsvHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(
            g_engine.pipeline_dx12.m_dsvHeap->GetCPUDescriptorHandleForHeapStart());
        dsvHandle.Offset(1, g_engine.pipeline_dx12.m_dsvDescriptorSize); // MSAA depth at index 1
        renderTarget = g_engine.pipeline_dx12.m_msaaRenderTargets[g_engine.sync_state.m_frameIndex];

        // Back buffer starts in PRESENT state for MSAA
        auto barrier1 = CD3DX12_RESOURCE_BARRIER::Transition(
            g_engine.pipeline_dx12.m_renderTargets[g_engine.sync_state.m_frameIndex],
            D3D12_RESOURCE_STATE_PRESENT,
            D3D12_RESOURCE_STATE_RESOLVE_DEST);
        g_engine.pipeline_dx12.m_commandList[g_engine.sync_state.m_frameIndex]->ResourceBarrier(1, &barrier1);
    }
    else
    {
        // Non-MSAA path
        rtvHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(
            g_engine.pipeline_dx12.m_rtvHeap->GetCPUDescriptorHandleForHeapStart(),
            (INT)g_engine.sync_state.m_frameIndex,
            g_engine.pipeline_dx12.m_rtvDescriptorSize);
        dsvHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(
            g_engine.pipeline_dx12.m_dsvHeap->GetCPUDescriptorHandleForHeapStart()); // Non-MSAA depth at index 0
        renderTarget = g_engine.pipeline_dx12.m_renderTargets[g_engine.sync_state.m_frameIndex];

        // Transition back buffer to RENDER_TARGET
        auto barrier1 = CD3DX12_RESOURCE_BARRIER::Transition(
            renderTarget,
            D3D12_RESOURCE_STATE_PRESENT,
            D3D12_RESOURCE_STATE_RENDER_TARGET);
        g_engine.pipeline_dx12.m_commandList[g_engine.sync_state.m_frameIndex]->ResourceBarrier(1, &barrier1);
    }

    // Common rendering operations
    g_engine.pipeline_dx12.m_commandList[g_engine.sync_state.m_frameIndex]->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);
    g_engine.pipeline_dx12.m_commandList[g_engine.sync_state.m_frameIndex]->ClearRenderTargetView(rtvHandle, g_rtClearValue.Color, 0, nullptr);
    g_engine.pipeline_dx12.m_commandList[g_engine.sync_state.m_frameIndex]->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 0.0f, 0, 0, nullptr);

    // Draw geometry (same for both MSAA)
    g_engine.pipeline_dx12.m_commandList[g_engine.sync_state.m_frameIndex]->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    PerDrawRootConstants currentDrawConstants = {};
    for (int i = 0; i < g_draw_list.drawAmount; ++i)
    {
        ObjectType objectType = g_draw_list.objectTypes[i];
        PrimitiveType currentPrimitiveToDraw = g_draw_list.primitiveTypes[i];
        UINT loadedModelIndex = g_draw_list.loadedModelIndex[i];
        RenderPipeline pl = g_draw_list.pipelines[i];

        UINT psoIndex = g_engine.msaa_state.m_enabled ? g_engine.msaa_state.m_currentSampleIndex : 0;
        bool enableAlphaForSky = true; // TODO: make this per object
        UINT activeBlendMode = (objectType == ObjectType::OBJECT_SKY_SPHERE && enableAlphaForSky) ? BLEND_ALPHA : BLEND_OPAQUE;
        ID3D12PipelineState *currentPSO = g_engine.pipeline_dx12.m_pipelineStates[pl][activeBlendMode][psoIndex];
        if (!currentPSO)
            SDL_Log("ERROR: PSO null for pipeline %d, msaa %d", pl, psoIndex);

        g_engine.pipeline_dx12.m_commandList[g_engine.sync_state.m_frameIndex]->SetPipelineState(currentPSO);

        // Translation parameters
        DirectX::XMVECTOR position = DirectX::XMLoadFloat3(&g_draw_list.transforms.pos[i]);
        DirectX::XMVECTOR rotationQuat = DirectX::XMLoadFloat4(&g_draw_list.transforms.rot[i]);
        DirectX::XMVECTOR scale = DirectX::XMLoadFloat3(&g_draw_list.transforms.scale[i]);

        // Build transform: SCALE * ROTATION * TRANSLATION (for row-major/left-multiply in HLSL)
        DirectX::XMMATRIX worldMatrix =
            DirectX::XMMatrixScalingFromVector(scale) *
            DirectX::XMMatrixRotationQuaternion(rotationQuat) * // rotation
            DirectX::XMMatrixTranslationFromVector(position);   // translation

        DirectX::XMStoreFloat4x4(
            &currentDrawConstants.world,
            DirectX::XMMatrixTranspose(worldMatrix));

        currentDrawConstants.heightmapIndex = g_draw_list.heightmapIndices[i];

        g_engine.pipeline_dx12.m_commandList[g_engine.sync_state.m_frameIndex]->SetGraphicsRoot32BitConstants(0, sizeof(PerDrawRootConstants) / 4, &currentDrawConstants, 0);

        if (objectType == OBJECT_PRIMITIVE || objectType == OBJECT_SKY_SPHERE)
        {
            g_engine.pipeline_dx12.m_commandList[g_engine.sync_state.m_frameIndex]->IASetVertexBuffers(0, 1, &g_engine.graphics_resources.m_vertexBufferView[currentPrimitiveToDraw]);
            g_engine.pipeline_dx12.m_commandList[g_engine.sync_state.m_frameIndex]->IASetIndexBuffer(&g_engine.graphics_resources.m_indexBufferView[currentPrimitiveToDraw]);
            g_engine.pipeline_dx12.m_commandList[g_engine.sync_state.m_frameIndex]->DrawIndexedInstanced(g_engine.graphics_resources.m_indexCount[currentPrimitiveToDraw], 1, 0, 0, 0);
        }
        else if (objectType == OBJECT_HEIGHTFIELD)
        {
            // Use shared heightfield mesh
            g_engine.pipeline_dx12.m_commandList[g_engine.sync_state.m_frameIndex]->IASetVertexBuffers(0, 1, &g_engine.graphics_resources.m_heightfieldVertexView);
            g_engine.pipeline_dx12.m_commandList[g_engine.sync_state.m_frameIndex]->IASetIndexBuffer(&g_engine.graphics_resources.m_heightfieldIndexView);
            g_engine.pipeline_dx12.m_commandList[g_engine.sync_state.m_frameIndex]->DrawIndexedInstanced(g_engine.graphics_resources.m_heightfieldIndexCount, 1, 0, 0, 0);
        }
        else if (objectType == OBJECT_LOADED_MODEL)
        {
            // todo unify outside this if statement
            currentDrawConstants.heightmapIndex = g_engine.graphics_resources.m_models[loadedModelIndex].textureIndex;
            g_engine.pipeline_dx12.m_commandList[g_engine.sync_state.m_frameIndex]->SetGraphicsRoot32BitConstants(0, sizeof(PerDrawRootConstants) / 4, &currentDrawConstants, 0);

            g_engine.pipeline_dx12.m_commandList[g_engine.sync_state.m_frameIndex]->IASetVertexBuffers(0, 1, &g_engine.graphics_resources.m_models[loadedModelIndex].vertexView);
            g_engine.pipeline_dx12.m_commandList[g_engine.sync_state.m_frameIndex]->IASetIndexBuffer(&g_engine.graphics_resources.m_models[loadedModelIndex].indexView);
            g_engine.pipeline_dx12.m_commandList[g_engine.sync_state.m_frameIndex]->DrawIndexedInstanced(g_engine.graphics_resources.m_models[loadedModelIndex].indexCount, 1, 0, 0, 0);
        }
    }

    // Debug: draw player collision cylinder (wireframe)
    if (g_show_player_wireframe)
    {
        UINT psoIndex = g_engine.msaa_state.m_enabled ? g_engine.msaa_state.m_currentSampleIndex : 0;
        ID3D12PipelineState *wirePSO = g_engine.pipeline_dx12.m_wireframePSO[psoIndex];
        if (wirePSO)
        {
            g_engine.pipeline_dx12.m_commandList[g_engine.sync_state.m_frameIndex]->SetPipelineState(wirePSO);

            // Compute cylinder centre from current camera position
            float yCentre = g_camera.position.y - g_player_bounds.eyeHeight + g_player_bounds.height * 0.5f;

            // Unit cylinder (radius 0.5, height 1) → scale to match player
            float scaleX = g_player_bounds.radius / 0.5f;
            float scaleY = g_player_bounds.height / 1.0f;
            float scaleZ = g_player_bounds.radius / 0.5f;

            DirectX::XMMATRIX world = DirectX::XMMatrixScaling(scaleX, scaleY, scaleZ) *
                                      DirectX::XMMatrixTranslation(g_camera.position.x, yCentre, g_camera.position.z);

            PerDrawRootConstants cylConstants;
            DirectX::XMStoreFloat4x4(&cylConstants.world, DirectX::XMMatrixTranspose(world));
            cylConstants.heightmapIndex = 0; // unused

            g_engine.pipeline_dx12.m_commandList[g_engine.sync_state.m_frameIndex]->SetGraphicsRoot32BitConstants(
                0, sizeof(PerDrawRootConstants) / 4, &cylConstants, 0);

            // Use the existing cylinder mesh
            PrimitiveType cylType = PRIMITIVE_CYLINDER;
            g_engine.pipeline_dx12.m_commandList[g_engine.sync_state.m_frameIndex]->IASetVertexBuffers(
                0, 1, &g_engine.graphics_resources.m_vertexBufferView[cylType]);
            g_engine.pipeline_dx12.m_commandList[g_engine.sync_state.m_frameIndex]->IASetIndexBuffer(
                &g_engine.graphics_resources.m_indexBufferView[cylType]);
            g_engine.pipeline_dx12.m_commandList[g_engine.sync_state.m_frameIndex]->DrawIndexedInstanced(
                g_engine.graphics_resources.m_indexCount[cylType], 1, 0, 0, 0);
        }
    }

    // Post-draw operations
    if (g_engine.msaa_state.m_enabled)
    {
        // MSAA: Resolve to back buffer
        auto barrier2 = CD3DX12_RESOURCE_BARRIER::Transition(g_engine.pipeline_dx12.m_msaaRenderTargets[g_engine.sync_state.m_frameIndex], D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
        g_engine.pipeline_dx12.m_commandList[g_engine.sync_state.m_frameIndex]->ResourceBarrier(1, &barrier2);

        g_engine.pipeline_dx12.m_commandList[g_engine.sync_state.m_frameIndex]->ResolveSubresource(g_engine.pipeline_dx12.m_renderTargets[g_engine.sync_state.m_frameIndex], 0, g_engine.pipeline_dx12.m_msaaRenderTargets[g_engine.sync_state.m_frameIndex], 0, DXGI_FORMAT_R8G8B8A8_UNORM);

        auto barrier3 = CD3DX12_RESOURCE_BARRIER::Transition(g_engine.pipeline_dx12.m_renderTargets[g_engine.sync_state.m_frameIndex], D3D12_RESOURCE_STATE_RESOLVE_DEST, D3D12_RESOURCE_STATE_RENDER_TARGET);
        g_engine.pipeline_dx12.m_commandList[g_engine.sync_state.m_frameIndex]->ResourceBarrier(1, &barrier3);

        auto barrier4 = CD3DX12_RESOURCE_BARRIER::Transition(g_engine.pipeline_dx12.m_msaaRenderTargets[g_engine.sync_state.m_frameIndex], D3D12_RESOURCE_STATE_RESOLVE_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
        g_engine.pipeline_dx12.m_commandList[g_engine.sync_state.m_frameIndex]->ResourceBarrier(1, &barrier4);
    }

    if (g_view_editor)
    {
        // ImGui rendering (always to back buffer)
        CD3DX12_CPU_DESCRIPTOR_HANDLE backBufferRtvHandle(
            g_engine.pipeline_dx12.m_rtvHeap->GetCPUDescriptorHandleForHeapStart(),
            (INT)g_engine.sync_state.m_frameIndex,
            g_engine.pipeline_dx12.m_rtvDescriptorSize);
        g_engine.pipeline_dx12.m_commandList[g_engine.sync_state.m_frameIndex]->OMSetRenderTargets(1, &backBufferRtvHandle, FALSE, nullptr);

        ImGui::Render();
        ID3D12DescriptorHeap *imguiHeaps[] = {g_imguiHeap.Heap};
        g_engine.pipeline_dx12.m_commandList[g_engine.sync_state.m_frameIndex]->SetDescriptorHeaps(_countof(imguiHeaps), imguiHeaps);
        ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), g_engine.pipeline_dx12.m_commandList[g_engine.sync_state.m_frameIndex]);
    }

    // Final transition to PRESENT
    auto finalBarrier = CD3DX12_RESOURCE_BARRIER::Transition(
        g_engine.pipeline_dx12.m_renderTargets[g_engine.sync_state.m_frameIndex],
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_PRESENT);
    g_engine.pipeline_dx12.m_commandList[g_engine.sync_state.m_frameIndex]->ResourceBarrier(1, &finalBarrier);

    if (!HRAssert(g_engine.pipeline_dx12.m_commandList[g_engine.sync_state.m_frameIndex]->Close()))
        return false;
    return true;
}

void Render(bool vsync = true)
{
    if (!PopulateCommandList())
        log_error("A command failed to be populated");

    ID3D12CommandList *ppCommandLists[] = {g_engine.pipeline_dx12.m_commandList[g_engine.sync_state.m_frameIndex]};
    g_engine.pipeline_dx12.m_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

    UINT syncInterval = (vsync) ? 1 : 0;
    UINT syncFlags = (vsync) ? 0 : DXGI_PRESENT_ALLOW_TEARING;
    HRAssert(g_engine.pipeline_dx12.m_swapChain->Present(syncInterval, syncFlags));
}

// this functions exists for a future where we will do more than just render the whole scene, this will include culling here
// TODO: update this function to group objects by rendering pipeline, and make sky objects be rendered last
void FillDrawList()
{
    // TODO (optimisation): this part is the static objects so make spatial structure and only fill when needed, not every frame
    int drawCount = 0;
    for (int i = 0; i < g_scene.objectCount && drawCount < g_draw_list_element_total; ++i)
    {
        const SceneObject &obj = g_scene.objects[i];
        if (obj.objectType != OBJECT_PRIMITIVE && obj.objectType != OBJECT_HEIGHTFIELD && obj.objectType != OBJECT_SKY_SPHERE && obj.objectType != OBJECT_LOADED_MODEL)
            continue;

        g_draw_list.transforms.pos[drawCount] = obj.pos;
        g_draw_list.transforms.rot[drawCount] = obj.rot;
        g_draw_list.transforms.scale[drawCount] = obj.scale;

        g_draw_list.objectTypes[drawCount] = obj.objectType;

        if (obj.objectType == OBJECT_PRIMITIVE)
        {
            g_draw_list.primitiveTypes[drawCount] = obj.data.primitive.primitiveType;
        }
        else if (obj.objectType == OBJECT_HEIGHTFIELD)
        {
            g_draw_list.heightmapIndices[drawCount] = g_engine.graphics_resources.m_sceneObjectIndices[i];
        }
        else if (obj.objectType == OBJECT_SKY_SPHERE)
        {
            g_draw_list.primitiveTypes[drawCount] = PRIMITIVE_INVERTED_SPHERE;
            g_draw_list.heightmapIndices[drawCount] = g_engine.graphics_resources.m_sceneObjectIndices[i];
        }
        else if (obj.objectType == OBJECT_LOADED_MODEL)
        {
            g_draw_list.loadedModelIndex[drawCount] = g_scene.objects[i].data.loaded_model.model_index;
            // g_draw_list.heightmapIndices[drawCount] = g_scene.objects[i].data.loaded_model.model_index;
        }

        g_draw_list.pipelines[drawCount] = obj.pipeline;
        drawCount++;
    }

    // TODO (optimisation): fill this part of the list every frame when enemies are active
    //  this part will be a flat array and all objects will be drawn regardless of position or overdraw because they are likely to be updated pretty much every frame (unless no bots are being simulated)
    for (int i = 0; i < MAX_BOT_OBJECTS; ++i)
    {
        const BotObjects &bot = g_bot_objects[i];
        DirectX::XMFLOAT3 scaleOverride = {};
        scaleOverride.x = 1;
        scaleOverride.y = 1;
        scaleOverride.z = 1;

        g_draw_list.transforms.pos[drawCount] = bot.pos;
        g_draw_list.transforms.rot[drawCount] = bot.rot;
        // g_draw_list.transforms.scale[drawCount] = bot.scale;        //TODO: have this setup on load
        g_draw_list.transforms.scale[drawCount] = scaleOverride;

        g_draw_list.objectTypes[drawCount] = ObjectType::OBJECT_PRIMITIVE;
        g_draw_list.primitiveTypes[drawCount] = PrimitiveType::PRIMITIVE_SPHERE;

        drawCount++;
    }
    g_draw_list.drawAmount = drawCount;
}

float SampleHeightmapWorldY(const SceneObject &obj, const DirectX::XMFLOAT3 &worldPos)
{
    HeightmapDataCPU cpu = g_heightmapDataCPU;
    if (!cpu.data)
        return obj.pos.y; // fallback

    // Transform world → local object space (no rotation, uniform XZ scale)
    float localX = (worldPos.x - obj.pos.x) / obj.scale.x; // obj.scale.x == obj.scale.z
    float localZ = (worldPos.z - obj.pos.z) / obj.scale.z;

    // Check if inside the heightmap’s square footprint ([-0.5, 0.5] in X and Z)
    if (localX < -0.5f || localX > 0.5f || localZ < -0.5f || localZ > 0.5f)
        return obj.pos.y; // outside – return base height

    // Convert to UV [0,1]
    float u = localX + 0.5f;
    float v = localZ + 0.5f;

    // Clamp to avoid edge artefacts
    u = min(max(u, 0.0f), 1.0f);
    v = min(max(v, 0.0f), 1.0f);

    // --- Bilinear interpolation ---
    // Convert UV to texel coordinates in [0, width-1] and [0, height-1]
    float u_tex = u * ((float)cpu.width - 1);
    float v_tex = v * ((float)cpu.height - 1);

    // Get integer texel indices and fractions
    UINT ix0 = (UINT)u_tex;
    UINT iy0 = (UINT)v_tex;
    UINT ix1 = min(ix0 + 1, cpu.width - 1);
    UINT iy1 = min(iy0 + 1, cpu.height - 1);
    float fx = u_tex - (float)ix0;
    float fy = v_tex - (float)iy0;

    // Fetch four surrounding texels (values 0‑255)
    float p00 = (float)cpu.data[iy0 * cpu.width + ix0];
    float p10 = (float)cpu.data[iy0 * cpu.width + ix1];
    float p01 = (float)cpu.data[iy1 * cpu.width + ix0];
    float p11 = (float)cpu.data[iy1 * cpu.width + ix1];

    // Interpolate along X first
    float p0 = (1.0f - fx) * p00 + fx * p10;
    float p1 = (1.0f - fx) * p01 + fx * p11;

    // Interpolate along Y
    float heightVal = (1.0f - fy) * p0 + fy * p1;

    // Convert 0‑255 → [0,1] and apply object's Y scale
    float h = heightVal / 255.0f;
    return obj.pos.y + h * obj.scale.y;
}

// Convert pitch (X), yaw (Y), roll (Z) in radians to a quaternion.
// Order of rotations: first pitch (X), then yaw (Y), then roll (Z).
inline DirectX::XMVECTOR EulerToQuaternion(float pitch, float yaw, float roll)
{
    using namespace DirectX;

    float halfPitch = pitch * 0.5f;
    float halfYaw = yaw * 0.5f;
    float halfRoll = roll * 0.5f;

    float sp = sinf(halfPitch);
    float cp = cosf(halfPitch);
    float sy = sinf(halfYaw);
    float cy = cosf(halfYaw);
    float sr = sinf(halfRoll);
    float cr = cosf(halfRoll);

    // Derived from q = q_roll * q_yaw * q_pitch
    float x = cr * cy * sp - sr * cp * sy;
    float y = cr * cp * sy + sr * cy * sp;
    float z = cy * cp * sr - cr * sy * sp;
    float w = cr * cy * cp + sr * sy * sp;

    return XMVectorSet(x, y, z, w);
}

struct Contact
{
    DirectX::XMFLOAT3 normal;
    float penetration;
    bool isGround;
};

void DrawDebugRay(const DirectX::XMFLOAT3 &start, const DirectX::XMFLOAT3 &end, bool hit, const DirectX::XMFLOAT3 &hitPoint)
{
    ImDrawList *dl = ImGui::GetBackgroundDrawList();
    if (!dl)
        return;

    // Project world to screen
    auto worldToScreen = [](const DirectX::XMFLOAT3 &world) -> ImVec2
    {
        DirectX::XMVECTOR v = DirectX::XMVectorSet(world.x, world.y, world.z, 1.0f);
        DirectX::XMVECTOR clip = XMVector4Transform(v, g_camera.viewMatrix);
        clip = XMVector4Transform(clip, g_camera.projectionMatrix);
        float w = DirectX::XMVectorGetW(clip);
        if (w <= 0.0f)
            return ImVec2(-1, -1);
        float x = (DirectX::XMVectorGetX(clip) / (float)w + 1.0f) * 0.5f * (float)g_engine.viewport_state.m_width;
        float y = (1.0f - (DirectX::XMVectorGetY(clip) / (float)w + 1.0f) * 0.5f) * (float)g_engine.viewport_state.m_height;
        return ImVec2(x, y);
    };

    ImVec2 s = worldToScreen(start);
    ImVec2 e = worldToScreen(end);
    if (s.x >= 0 && e.x >= 0)
    {
        dl->AddLine(s, e, IM_COL32(255, 0, 0, 255), 2.0f);
    }
    if (hit)
    {
        ImVec2 hp = worldToScreen(hitPoint);
        if (hp.x >= 0)
        {
            dl->AddCircleFilled(hp, 6.0f, IM_COL32(0, 255, 0, 200));
            dl->AddCircle(hp, 6.0f, IM_COL32(255, 255, 255, 255), 0, 1.5f);
        }
    }
}

void DebugFireShot()
{
    // Camera eye and forward

    DirectX::XMFLOAT3 origin, dir;
    XMStoreFloat3(&origin, g_camera.eye);
    XMStoreFloat3(&dir, g_camera.forward);

    float maxDist = 500.0f;
    DirectX::XMFLOAT3 rayEnd = {
        origin.x + dir.x * maxDist,
        origin.y + dir.y * maxDist,
        origin.z + dir.z * maxDist};

    float closestDist = FLT_MAX;
    int hitIndex = -1;
    DirectX::XMFLOAT3 hitPoint = {0, 0, 0};

    for (int i = 0; i < MAX_BOT_OBJECTS; ++i)
    {
        const BotObjects &bot = g_bot_objects[i];
        float radius = 0.5f;

        float dx = bot.pos.x - origin.x;
        float dy = bot.pos.y - origin.y;
        float dz = bot.pos.z - origin.z;

        float t = dx * dir.x + dy * dir.y + dz * dir.z;
        if (t < 0)
            continue;

        float closestX = origin.x + dir.x * t;
        float closestY = origin.y + dir.y * t;
        float closestZ = origin.z + dir.z * t;

        float distSq = (closestX - bot.pos.x) * (closestX - bot.pos.x) +
                       (closestY - bot.pos.y) * (closestY - bot.pos.y) +
                       (closestZ - bot.pos.z) * (closestZ - bot.pos.z);

        if (distSq <= radius * radius)
        {
            float hitDist = sqrtf((closestX - origin.x) * (closestX - origin.x) +
                                  (closestY - origin.y) * (closestY - origin.y) +
                                  (closestZ - origin.z) * (closestZ - origin.z));
            if (hitDist < closestDist)
            {
                closestDist = hitDist;
                hitIndex = i;
                hitPoint = {closestX, closestY, closestZ};
            }
        }
    }

    if (hitIndex != -1)
    {
        SDL_Log("HIT BOT %d at distance %.2f", hitIndex, closestDist);
        rayEnd = hitPoint;
        BotObjects &bot = g_bot_objects[hitIndex];
        bot.state = BOT_DYING;
        // bot.velocity = {0, 0, 0}; // stop any residual movement (optional)
    }
    else
        SDL_Log("MISS");

    // Store the ray for drawing in editor
    g_debugRay.valid = true;
    g_debugRay.start = origin;
    g_debugRay.end = rayEnd;
    g_debugRay.hit = (hitIndex != -1);
    g_debugRay.hitPoint = hitPoint;
}

void Update()
{
    if (g_input.lmbDown)
    {
        DebugFireShot();
    }

    UpdateBots((float)program_state.timing.deltaTime);

    DirectX::XMFLOAT3 oldEye = g_camera.position;

    // Apply input to get tentative new eye
    g_camera.UpdateFlyCamera((float)program_state.timing.deltaTime);
    DirectX::XMFLOAT3 newEye = g_camera.position;

    // Compute tentative cylinder centre
    float eyeHeight = g_player_bounds.eyeHeight;
    float playerHeight = g_player_bounds.height;
    float radius = g_player_bounds.radius;

    DirectX::XMFLOAT3 centre;
    centre.x = newEye.x;
    centre.y = newEye.y - eyeHeight + playerHeight * 0.5f;
    centre.z = newEye.z;

    // Iterative wall resolution (cubes only)
    const float WALKABLE_THRESHOLD = 0.1f;
    const int MAX_CONTACTS = 128;
    const int MAX_ITER = 10;
    int iter = 0;
    bool anyWall = true;

    while (anyWall && iter < MAX_ITER)
    {
        anyWall = false;
        Contact contacts[MAX_CONTACTS];
        int contactCount = 0;

        // Collect contacts at current centre
        for (int i = 0; i < g_scene.objectCount && contactCount < MAX_CONTACTS; ++i)
        {
            const SceneObject &obj = g_scene.objects[i];
            if (obj.objectType != OBJECT_PRIMITIVE || (obj.data.primitive.primitiveType != PRIMITIVE_CUBE && obj.data.primitive.primitiveType != PRIMITIVE_SPHERE && obj.data.primitive.primitiveType != PRIMITIVE_CYLINDER))
                continue;

            if (obj.objectType == OBJECT_PRIMITIVE)
            {
                // TODO: Separate out this into a struct which could be the "environment specific player bounds" or something like that
                DirectX::XMFLOAT3 fakeCentre = centre;
                fakeCentre.y += g_stepHeight * 0.5f;
                float fakePlayerHeight = playerHeight - g_stepHeight;
                DirectX::XMFLOAT3 normal = {};
                float penetration = 0.0f;
                bool overlap = false;
                PrimitiveType pt = obj.data.primitive.primitiveType;
                if (pt == PRIMITIVE_CUBE)
                    overlap = OverlapCylinderCubeContact(fakeCentre, radius, fakePlayerHeight, obj, normal, penetration);
                else if (pt == PRIMITIVE_SPHERE)
                    overlap = OverlapCylinderSphereContact(fakeCentre, radius, fakePlayerHeight, obj, normal, penetration);
                else if (pt == PRIMITIVE_CYLINDER)
                    overlap = OverlapCylinderCylinderUpright(fakeCentre, radius, fakePlayerHeight, obj.pos, obj.scale.x * 0.5f, obj.scale.y, normal, penetration);
                else
                    overlap = false; // no collision for unimplemented shapes

                if (overlap)
                {
                    contacts[contactCount].normal = normal;
                    contacts[contactCount].penetration = penetration;
                    contacts[contactCount].isGround = (normal.y > WALKABLE_THRESHOLD);
                    contactCount++;
                }
            }
        }

        // Compute total horizontal correction for walls
        DirectX::XMFLOAT3 correction = {0, 0, 0};
        for (int c = 0; c < contactCount; ++c)
        {
            if (contacts[c].isGround)
                continue; // ground contacts handled later

            anyWall = true;

            // Horizontal component of normal
            DirectX::XMFLOAT3 horNormal = contacts[c].normal;
            horNormal.y = 0;
            float len = sqrtf(horNormal.x * horNormal.x + horNormal.z * horNormal.z);
            if (len < 1e-6f)
                continue;
            horNormal.x /= len;
            horNormal.z /= len;

            correction.x += horNormal.x * contacts[c].penetration;
            correction.z += horNormal.z * contacts[c].penetration;
        }

        if (correction.x == 0 && correction.z == 0)
            break;

        // Apply correction and continue iterating
        centre.x += correction.x;
        centre.z += correction.z;
        iter++;
    }

    g_camera.position.x = centre.x;
    g_camera.position.z = centre.z;

    // Ground detection (vertical ray)
    float feetY = centre.y - playerHeight * 0.5f;
    float bestGroundY = -FLT_MAX;

    for (int i = 0; i < g_scene.objectCount; ++i)
    {
        const SceneObject &obj = g_scene.objects[i];

        if (obj.objectType == OBJECT_HEIGHTFIELD)
        {
            float groundY = SampleHeightmapWorldY(obj, {centre.x, 0, centre.z});
            if (groundY > bestGroundY)
                bestGroundY = groundY;
        }
        else if (obj.objectType == OBJECT_PRIMITIVE)
        {
            DirectX::XMFLOAT3 rayOrigin = {centre.x, feetY + g_stepHeight, centre.z};
            DirectX::XMFLOAT3 rayDir = {0, -1, 0};

            float tMin, tMax;
            bool intersection = false;
            switch (obj.data.primitive.primitiveType)
            {
            case PRIMITIVE_CUBE:
                intersection = IntersectRayCube(rayOrigin, rayDir, obj, tMin, tMax);
                break;
            case PRIMITIVE_CYLINDER:
                intersection = IntersectRayCylinder(rayOrigin, rayDir, obj, tMin, tMax);
                break;
            case PRIMITIVE_SPHERE:
                intersection = IntersectRaySphere(rayOrigin, rayDir, obj, tMin, tMax);
                break;
            case PRIMITIVE_PRISM:
                intersection = IntersectRayPrism(rayOrigin, rayDir, obj, tMin, tMax);
                break;
            default:
                continue;
            }

            if (intersection)
            {
                float tHit = (tMin >= 0.0f) ? tMin : (tMax >= 0.0f ? tMax : -1.0f);
                if (tHit >= 0.0f)
                {
                    float hitY = rayOrigin.y - tHit;
                    if (hitY > bestGroundY)
                        bestGroundY = hitY;
                }
            }
        }
    }

    // If no ground found, keep current feet Y (fallback)
    if (bestGroundY == -FLT_MAX)
        bestGroundY = feetY;

    g_camera.position.y = bestGroundY + eyeHeight; // set final Y, TODO: this should probably be better, like player position rather than camera

    // Update view/projection matrices
    g_camera.UpdateViewProjMatrices(0.1f, 4192.0f);

    // TRANSPOSE before storing
    DirectX::XMStoreFloat4x4(&g_engine.graphics_resources.m_PerFrameConstantBufferData[g_engine.sync_state.m_frameIndex].view,
                             DirectX::XMMatrixTranspose(g_camera.viewMatrix));
    DirectX::XMStoreFloat4x4(&g_engine.graphics_resources.m_PerFrameConstantBufferData[g_engine.sync_state.m_frameIndex].projection,
                             DirectX::XMMatrixTranspose(g_camera.projectionMatrix));

    memcpy(g_engine.graphics_resources.m_pCbvDataBegin[g_engine.sync_state.m_frameIndex],
           &g_engine.graphics_resources.m_PerFrameConstantBufferData[g_engine.sync_state.m_frameIndex],
           sizeof(PerFrameConstantBuffer));

    FillDrawList();
}

// Convert quaternion → pitch/yaw/roll (radians), order: pitch (X), yaw (Y), roll (Z)
inline void QuaternionToEuler(DirectX::FXMVECTOR Q, float &pitch, float &yaw, float &roll)
{
    constexpr float PI = 3.14159265358979323846f;
    constexpr float HALF_PI = PI * 0.5f;

    DirectX::XMFLOAT4 q;
    DirectX::XMStoreFloat4(&q, Q);
    float x = q.x, y = q.y, z = q.z, w = q.w;

    // pitch (x-axis rotation)
    float sinp = 2.0f * (w * x + y * z);
    float cosp = 1.0f - 2.0f * (x * x + y * y);

    if (cosp == 0.0f)
    {
        pitch = (sinp >= 0.0f) ? HALF_PI : -HALF_PI;
    }
    else
    {
        pitch = atanf(sinp / cosp);
        if (cosp < 0.0f)
        {
            if (sinp >= 0.0f)
                pitch += PI;
            else
                pitch -= PI;
        }
    }

    // yaw (y-axis rotation)
    float siny = 2.0f * (w * y - z * x);
    if (fabsf(siny) >= 1.0f)
        yaw = copysignf(HALF_PI, siny);
    else
        yaw = asinf(siny);

    // roll (z-axis rotation)
    float sinr = 2.0f * (w * z + x * y);
    float cosr = 1.0f - 2.0f * (y * y + z * z);

    if (cosr == 0.0f)
    {
        roll = (sinr >= 0.0f) ? HALF_PI : -HALF_PI;
    }
    else
    {
        roll = atanf(sinr / cosr);
        if (cosr < 0.0f)
        {
            if (sinr >= 0.0f)
                roll += PI;
            else
                roll -= PI;
        }
    }
}

// editor state
static int g_selectedObjectIndex = 0;

void DrawBotsEditorGUI()
{
    ImGui::Begin("Bots");
    ImGui::Text("Total bots: %d", MAX_BOT_OBJECTS);
    ImGui::Separator();

    // Use a child region to make the list scrollable
    ImGui::BeginChild("BotList", ImVec2(0, 400), true);

    for (int i = 0; i < MAX_BOT_OBJECTS; ++i)
    {
        BotObjects &bot = g_bot_objects[i];
        ImGui::PushID(i);

        // Collapsible header with bot index and current position
        bool open = ImGui::CollapsingHeader(("Bot " + std::to_string(i)).c_str());

        // Show minimal info even when collapsed
        ImGui::SameLine();
        ImGui::TextDisabled("(%.1f, %.1f, %.1f)", bot.pos.x, bot.pos.y, bot.pos.z);

        if (open)
        {
            // ---- Read-only runtime state ----
            ImGui::Text("Current target index: %d", bot.currentPatrolPointTargetIndex);
            ImGui::Text("Waiting: %s", bot.isWaiting ? "yes" : "no");
            ImGui::Text("Wait remaining: %.2f s", bot.waitTimeRemaining);

            ImGui::Separator();

            // ---- Editable fields ----
            // Position
            if (ImGui::DragFloat3("Position", &bot.pos.x, 0.1f))
            {
                // Optionally clamp or adjust
            }

            // Patrol point count
            int newCount = bot.patrolPointCount;
            if (ImGui::SliderInt("Patrol point count", &newCount, 1, maxPatrolPoints))
            {
                if (newCount != bot.patrolPointCount)
                {
                    // When increasing, initialize new points to current position + offset
                    for (int p = bot.patrolPointCount; p < newCount; ++p)
                    {
                        bot.patrolPoints[p] = bot.pos;
                        // add a small random offset so they're not all on top of each other
                        bot.patrolPoints[p].x += (float)(rand() % 20) - 10.0f;
                        bot.patrolPoints[p].z += (float)(rand() % 20) - 10.0f;
                        bot.patrolPoints[p].y += (float)(rand() % 10) - 5.0f;
                    }
                    bot.patrolPointCount = newCount;
                    // Clamp current target index
                    if (bot.currentPatrolPointTargetIndex >= newCount)
                        bot.currentPatrolPointTargetIndex = 0;
                }
            }

            // Patrol points list
            for (int p = 0; p < bot.patrolPointCount; ++p)
            {
                ImGui::PushID(p);
                if (ImGui::DragFloat3(("Point " + std::to_string(p)).c_str(), &bot.patrolPoints[p].x, 0.1f))
                {
                    // Optional: clamp or do nothing
                }
                ImGui::PopID();
            }

            // Patrol mode combo
            const char *modeNames[] = {"Wrap", "Reverse Direction"};
            int currentMode = (bot.patrolMode == PATROL_WRAP) ? 0 : 1;
            if (ImGui::Combo("Patrol mode", &currentMode, modeNames, 2))
            {
                bot.patrolMode = (currentMode == 0) ? PATROL_WRAP : PATROL_REVERSE_DIRECTION;
                // Reset direction for reverse mode if needed
                if (bot.patrolMode == PATROL_REVERSE_DIRECTION)
                    bot.patrolDirection = 1;
            }

            // Speed
            ImGui::DragFloat("Speed", &bot.speed, 0.1f, 0.1f, 50.0f);

            // Wait time at each point
            ImGui::DragFloat("Wait seconds", &bot.waitTimeSeconds, 0.1f, 0.0f, 10.0f);

            ImGui::Separator();
        }

        ImGui::PopID();
    }

    ImGui::EndChild();
    ImGui::End();
}

void DrawEditorGUI()
{
    ImGui::NewFrame();

    if (g_debugRay.valid)
    {
        DrawDebugRay(g_debugRay.start, g_debugRay.end, g_debugRay.hit, g_debugRay.hitPoint);
        // g_debugRay.valid = false;
    }

    // gizmos
    // ============================================
    // ImGuizmo – using stored quaternion directly
    // ============================================
    ImGuizmo::BeginFrame();
    ImGuizmo::Enable(true);
    ImGuizmo::SetRect(0, 0, (float)g_engine.viewport_state.m_width, (float)g_engine.viewport_state.m_height);

    if (g_selectedObjectIndex >= 0 && g_selectedObjectIndex < g_scene.objectCount)
    {
        auto &obj = g_scene.objects[g_selectedObjectIndex];

        // ---- Build world matrix from pos, quaternion, scale (row‑major) ----
        DirectX::XMMATRIX scale = DirectX::XMMatrixScaling(obj.scale.x, obj.scale.y, obj.scale.z);
        DirectX::XMVECTOR rotQuat = DirectX::XMLoadFloat4(&obj.rot);
        DirectX::XMMATRIX rotation = DirectX::XMMatrixRotationQuaternion(rotQuat);
        DirectX::XMMATRIX translation = DirectX::XMMatrixTranslation(obj.pos.x, obj.pos.y, obj.pos.z);
        DirectX::XMMATRIX world = scale * rotation * translation; // row‑major

        // ---- ImGuizmo expects row‑major float[4][4] – pass directly ----
        float *viewPtr = (float *)&g_camera.viewMatrix;
        float *projPtr = (float *)&g_camera.projectionMatrix;
        float *worldPtr = (float *)&world;

        // ---- Draw gizmo ----
        ImGuizmo::Manipulate(viewPtr, projPtr,
                             ImGuizmo::OPERATION::TRANSLATE | (ImGuizmo::ROTATE_X | ImGuizmo::ROTATE_Y | ImGuizmo::ROTATE_Z) | ImGuizmo::OPERATION::SCALE,
                             ImGuizmo::MODE::WORLD,
                             worldPtr);

        // ---- Apply changes ----
        if (ImGuizmo::IsUsing())
        {
            // world has been modified in place (still row‑major)
            DirectX::XMVECTOR scaleVec, rotQuatNew, posVec;
            DirectX::XMMatrixDecompose(&scaleVec, &rotQuatNew, &posVec, world);

            // Position
            DirectX::XMStoreFloat3(&obj.pos, posVec);

            // Rotation – store quaternion directly (no Euler conversion!)
            DirectX::XMStoreFloat4(&obj.rot, rotQuatNew);

            // Scale
            DirectX::XMStoreFloat3(&obj.scale, scaleVec);

            // Persist change
            write_scene();
        }
    }

    ImGui::Begin("Debug Controls");
    ImGui::Text("Camera Pos: {%.3f, %.3f, %.3f}", g_camera.position.x, g_camera.position.y, g_camera.position.z);
    ImGui::Checkbox("Show Player Cylinder", &g_show_player_wireframe);
    ImGui::End();

    ImGui::Begin("Settings");
    ImGui::SliderFloat("fov_deg", &g_camera.fov_deg, 60.0f, 120.0f);
    ImGui::Text("Frametime %.3f ms (%.2f FPS)",
                1000.0f / ImGui::GetIO().Framerate,
                ImGui::GetIO().Framerate);

    if (ImGui::Button("Exit"))
    {
        program_state.isRunning = false;
    }

    // MSAA settings
    ImGui::Separator();
    ImGui::Text("MSAA");

    UINT oldIndex = g_engine.msaa_state.m_currentSampleIndex;

    // Build the current selection string
    char currentSelection[32];
    if (g_engine.msaa_state.m_currentSampleIndex == 0)
    {
        snprintf(currentSelection, sizeof(currentSelection), "Disabled (1x)");
    }
    else
    {
        snprintf(currentSelection, sizeof(currentSelection), "%dx MSAA",
                 g_engine.msaa_state.m_sampleCounts[g_engine.msaa_state.m_currentSampleIndex]);
    }

    if (ImGui::BeginCombo("Anti-aliasing", currentSelection))
    {
        {
            bool isSelected = (g_engine.msaa_state.m_currentSampleIndex == 0);
            if (ImGui::Selectable("Disabled (1x)", isSelected))
            {
                g_engine.msaa_state.m_currentSampleIndex = 0;
                g_engine.msaa_state.m_currentSampleCount = 1;
                g_engine.msaa_state.m_enabled = false;
            }
            if (isSelected)
            {
                ImGui::SetItemDefaultFocus();
            }
        }

        // Show MSAA options
        for (UINT i = 1; i < 4; i++)
        {
            bool isSelected = (g_engine.msaa_state.m_currentSampleIndex == i);
            bool isSupported = g_engine.msaa_state.m_supported[i];

            char itemLabel[32];
            if (isSupported)
            {
                snprintf(itemLabel, sizeof(itemLabel), "%dx MSAA", g_engine.msaa_state.m_sampleCounts[i]);
            }
            else
            {
                snprintf(itemLabel, sizeof(itemLabel), "%dx MSAA (unsupported)", g_engine.msaa_state.m_sampleCounts[i]);
                ImGui::BeginDisabled();
            }

            if (ImGui::Selectable(itemLabel, isSelected) && isSupported)
            {
                g_engine.msaa_state.m_currentSampleIndex = i;
                g_engine.msaa_state.m_currentSampleCount = g_engine.msaa_state.m_sampleCounts[i];
                g_engine.msaa_state.m_enabled = true;
            }

            if (!isSupported)
            {
                ImGui::EndDisabled();
            }

            if (isSelected)
            {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    // Handle MSAA changes
    if (oldIndex != g_engine.msaa_state.m_currentSampleIndex)
    {
        SDL_Log("MSAA settings changed: %s, %dx",
                g_engine.msaa_state.m_enabled ? "enabled" : "disabled",
                g_engine.msaa_state.m_currentSampleCount);
        RecreateMSAAResources();
        if (g_engine.msaa_state.m_enabled)
        {
            g_liveConfigData.GraphicsSettings.msaa_level = (int)g_engine.msaa_state.m_currentSampleCount;
        }
        else
        {
            g_liveConfigData.GraphicsSettings.msaa_level = 1;
        }
        SaveConfig(&g_liveConfigData);
    }

    ImGui::Separator();

    ImGui::Text("Window Mode");

    static const char *windowModeNames[(const uint32_t)WindowMode::NUM_WINDOW_MODES] = {"Windowed", "Borderless"};
    if (ImGui::BeginCombo("Mode", windowModeNames[(UINT)program_state.window.m_currentMode]))
    {
        for (int i = 0; i < (const uint32_t)WindowMode::NUM_WINDOW_MODES; i++)
        {
            bool isSelected = (program_state.window.m_currentMode == (WindowMode)i);
            if (ImGui::Selectable(windowModeNames[i], isSelected))
            {
                window_request.requestedMode = (WindowMode)i;
                window_request.applyWindowRequest = true;
            }
            if (isSelected)
            {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    ImGui::Separator();

    static int currentResItem = -1;
    static const int numSupportedResolutions = 6;
    static struct
    {
        char *resNames[numSupportedResolutions] = {"1280x720", "1920x1080", "640x480", "1024x768", "1680x720", "1280x800 (steamdeck)"};
        uint32_t w[numSupportedResolutions] = {1280, 1920, 640, 1024, 1680, 1280};
        uint32_t h[numSupportedResolutions] = {720, 1080, 480, 768, 720, 800};
    } supported_window_dimensions;
    if (program_state.window.m_currentMode == WindowMode::WINDOWED &&
        ImGui::BeginCombo("Resolution", (currentResItem == -1) ? "." : supported_window_dimensions.resNames[currentResItem]))
    {
        for (int i = 0; i < numSupportedResolutions; i++)
        {
            bool isSelected = (i == currentResItem);
            if (ImGui::Selectable(supported_window_dimensions.resNames[i], isSelected))
            {
                if (currentResItem != i)
                {
                    window_request.applyWindowRequest = true;
                    program_state.window.m_windowWidth = supported_window_dimensions.w[i];
                    program_state.window.m_windowHeight = supported_window_dimensions.h[i];
                }
                currentResItem = i;
            }
            if (isSelected)
            {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    if (ImGui::Checkbox("Vsync", (bool *)&g_liveConfigData.GraphicsSettings.vsync))
    {
        SaveConfig(&g_liveConfigData);
    }
    ImGui::Separator();

    ImGui::End();

    ImGui::Begin("Globals");
    static float g_ambient_color[3] = {0.0f, 0.0f, 0.0f};
    ImGui::Text("Ambient Color");
    if (ImGui::ColorEdit3("Ambient", g_ambient_color))
    {
        // Update the per-scene constant buffer when color changes
        g_engine.graphics_resources.m_PerSceneConstantBufferData.ambient_colour =
            DirectX::XMFLOAT4(g_ambient_color[0], g_ambient_color[1], g_ambient_color[2], 1.0f);

        // Copy to GPU memory
        memcpy(g_engine.graphics_resources.m_pPerSceneCbvDataBegin,
               &g_engine.graphics_resources.m_PerSceneConstantBufferData,
               sizeof(g_engine.graphics_resources.m_PerSceneConstantBufferData));
    }

    ImGui::End();

    // ============================================
    // Scene Objects Editor – editing g_scene
    // ============================================

    ImGui::Begin("Scene Objects");
    ImGui::Text("Total objects: %d", g_scene.objectCount);

    if (ImGui::Button("Add Object") && g_scene.objectCount < MAX_SCENE_OBJECTS)
    {
        int idx = g_scene.objectCount;
        SceneObject *obj = &g_scene.objects[idx];
        strcpy_s(obj->nametag, sizeof(obj->nametag), "");
        obj->pos = {0.0f, 0.0f, 0.0f};
        obj->rot = {0.0f, 0.0f, 0.0f, 1.0f};
        obj->scale = {1.0f, 1.0f, 1.0f};
        obj->data.primitive.primitiveType = PRIMITIVE_CUBE;
        obj->objectType = OBJECT_PRIMITIVE;
        g_scene.objectCount++;
        write_scene();
        g_selectedObjectIndex = idx;
    }
    ImGui::SameLine();
    ImGui::Text("(max %d)", MAX_SCENE_OBJECTS);

    for (int i = 0; i < g_scene.objectCount; ++i)
    {
        ImGui::PushID(i);
        auto &obj = g_scene.objects[i];

        // --- TreeNode with fixed label + name display ---
        ImGuiTreeNodeFlags node_flags = ImGuiTreeNodeFlags_OpenOnArrow |
                                        ImGuiTreeNodeFlags_OpenOnDoubleClick |
                                        ImGuiTreeNodeFlags_SpanAvailWidth;
        if (i == g_selectedObjectIndex)
            node_flags |= ImGuiTreeNodeFlags_Selected;

        // Use FIXED label "Object" - identity never changes NOTE: DONT CHANGE THIS LABEL TO ANYTHING ELSE!!!!!!!
        bool node_open = ImGui::TreeNodeEx("Object", node_flags);

        // Check for click on the tree node
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
        {
            g_selectedObjectIndex = i;
        }

        // Show the actual name on the same line
        ImGui::SameLine();
        if (obj.nametag[0] != '\0' && strcmp(obj.nametag, "Unamed") != 0)
            ImGui::TextUnformatted(obj.nametag);
        else
            ImGui::Text("Unnamed");

        if (node_open)
        {
            // Editable name field (already there)
            ImGui::InputText("Name", obj.nametag, IM_ARRAYSIZE(obj.nametag));
            if (ImGui::IsItemDeactivatedAfterEdit())
                write_scene();

            // --- Object type selector ---
            int currentType = (int)obj.objectType;
            if (ImGui::Combo("Type", &currentType, g_objectTypeNames, OBJECT_COUNT))
            {
                ObjectType newType = (ObjectType)currentType;
                if (newType != obj.objectType)
                {
                    // Clear the union before switching (important!)
                    memset(&obj.data, 0, sizeof(obj.data));
                    obj.objectType = newType;

                    // set defaults
                    switch (newType)
                    {
                    case OBJECT_PRIMITIVE:
                    {
                        obj.data.primitive.primitiveType = PRIMITIVE_CUBE;
                        obj.pipeline = RENDER_DEFAULT; // default pipeline
                    }
                    break;
                    case OBJECT_HEIGHTFIELD:
                    {
                        obj.data.heightfield.width = 256; // example default
                    }
                    break;
                    case OBJECT_LOADED_MODEL:
                    {
                        strcpy_s(obj.data.loaded_model.pathTo, sizeof(obj.data.loaded_model.pathTo), "");
                    }
                    break;
                    case OBJECT_SKY_SPHERE:
                    {
                        strcpy_s(obj.data.sky_sphere.pathToTexture, sizeof(obj.data.sky_sphere.pathToTexture), "");
                        obj.pipeline = RENDER_SKY;
                    }
                    break;
                    case OBJECT_WATER:
                    {
                        obj.data.water.choppiness = 1.0f;
                    }
                    break;
                    default:
                        break;
                    }
                }
            }
            if (obj.objectType == OBJECT_PRIMITIVE)
            {
                ImGui::Indent(16.0f);
                int currentPrimitive = (int)obj.data.primitive.primitiveType;
                if (ImGui::Combo("Primitive", &currentPrimitive,
                                 g_primitiveNames, PRIMITIVE_COUNT))
                {
                    obj.data.primitive.primitiveType = (PrimitiveType)currentPrimitive;
                    write_scene();
                }
                ImGui::Unindent(16.0f);
            }

            // ---- Pipeline dropdown ----
            int currentPipeline = (int)obj.pipeline;
            if (ImGui::Combo("Pipeline", &currentPipeline,
                             g_renderPipelineNames, RENDER_COUNT))
            {
                obj.pipeline = (RenderPipeline)currentPipeline;
            }

            ImGui::DragFloat3("Position", &obj.pos.x, 0.1f);

            // ---- Rotation (quaternion → Euler sliders with immediate update) ----
            DirectX::XMFLOAT4 q = obj.rot;
            DirectX::XMVECTOR Q = XMLoadFloat4(&q);
            float pitch, yaw, roll;
            QuaternionToEuler(Q, pitch, yaw, roll);

            float pitchDeg = DirectX::XMConvertToDegrees(pitch);
            float yawDeg = DirectX::XMConvertToDegrees(yaw);
            float rollDeg = DirectX::XMConvertToDegrees(roll);

            bool canRotate = true;
            if (obj.objectType == OBJECT_HEIGHTFIELD)
                canRotate = false;
            if (obj.objectType == OBJECT_PRIMITIVE && obj.data.primitive.primitiveType == PRIMITIVE_CYLINDER)
                canRotate = false;

            bool rotationChanged = false;
            if (canRotate)
            {
                rotationChanged |= ImGui::DragFloat("Pitch", &pitchDeg, 0.5f, -180.0f, 180.0f, "%.1f°");
                rotationChanged |= ImGui::DragFloat("Yaw", &yawDeg, 0.5f, -180.0f, 180.0f, "%.1f°");
                rotationChanged |= ImGui::DragFloat("Roll", &rollDeg, 0.5f, -180.0f, 180.0f, "%.1f°");
            }

            if (rotationChanged)
            {
                float p = DirectX::XMConvertToRadians(pitchDeg);
                float y = DirectX::XMConvertToRadians(yawDeg);
                float r = DirectX::XMConvertToRadians(rollDeg);
                DirectX::XMVECTOR Q_ = DirectX::XMQuaternionRotationRollPitchYaw(p, y, r);
                XMStoreFloat4(&obj.rot, Q_);
            }

            ImGui::DragFloat3("Scale", &obj.scale.x, 0.01f, 0.01f, 10.0f);
            // If sphere or cylinder, enforce uniform scale
            if (obj.objectType == OBJECT_PRIMITIVE)
            {
                float uniform = obj.scale.x;

                if (obj.data.primitive.primitiveType == PRIMITIVE_SPHERE)
                    obj.scale.y = obj.scale.z = uniform;
                if (obj.data.primitive.primitiveType == PRIMITIVE_CYLINDER)
                    obj.scale.z = uniform;
            }
            ImGui::SameLine();
            if (ImGui::Button("Duplicate"))
            {
                if (g_scene.objectCount < MAX_SCENE_OBJECTS)
                {
                    g_scene.objects[g_scene.objectCount] = obj;
                    g_scene.objectCount++;
                }
            }

            // Persist changes
            write_scene();

            ImGui::TreePop();
        }

        ImGui::Separator();
        ImGui::PopID();
    }
    ImGui::End();

    DrawBotsEditorGUI();
}

void LoadAllTextures()
{
    std::vector<ID3D12Resource *> localUploadHeaps;

    g_engine.pipeline_dx12.m_commandAllocators[0]->Reset();
    g_engine.pipeline_dx12.m_commandList[0]->Reset(g_engine.pipeline_dx12.m_commandAllocators[0], nullptr);

    for (int i = 0; i < g_scene.objectCount; ++i)
    {
        const auto &obj = g_scene.objects[i];

        if (obj.objectType == OBJECT_HEIGHTFIELD)
        {
            const char *path = obj.data.heightfield.pathToHeightmap;
            UINT &outIndex = g_engine.graphics_resources.m_sceneObjectIndices[i];
            UINT errorIndex = g_errorHeightmapIndex;

            // set rotation of heightmap to zero as given in decisiona
            DirectX::XMStoreFloat4(&g_scene.objects[i].rot, DirectX::XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f));
            if (path[0] != '\0')
            {
                ID3D12Resource *tex = nullptr; // not used directly
                TextureLoadResult tl = LoadTextureFromFile(g_engine.pipeline_dx12.m_device, g_engine.pipeline_dx12.m_commandList[0], path, &tex, true);
                if (tl.success)
                {
                    SDL_Log("Loaded heightmap: %s, index %u", path, tl.outIndex);
                    outIndex = tl.outIndex;
                    if (tl.uploadHeap)
                        localUploadHeaps.push_back(tl.uploadHeap);

                    g_heightmapDataCPU.data = tl.cpu_copy.data;
                    g_heightmapDataCPU.width = tl.cpu_copy.width;
                    g_heightmapDataCPU.height = tl.cpu_copy.height;
                }
                else
                {
                    SDL_Log("Failed to load heightmap: %s, using error texture", path);
                    outIndex = errorIndex;
                }
            }
            else
            {
                outIndex = errorIndex;
            }
        }
        else if (obj.objectType == OBJECT_SKY_SPHERE)
        {
            const char *path = obj.data.sky_sphere.pathToTexture;
            UINT &outIndex = g_engine.graphics_resources.m_sceneObjectIndices[i];
            UINT errorIndex = 0; // assuming index 0 is the error texture

            if (path[0] != '\0')
            {
                ID3D12Resource *tex = nullptr;
                TextureLoadResult tl = LoadSkyTextureFromFile(g_engine.pipeline_dx12.m_device,
                                                              g_engine.pipeline_dx12.m_commandList[0],
                                                              path, &tex);
                if (tl.success)
                {
                    SDL_Log("Loaded sky texture: %s, index %u", path, tl.outIndex);
                    outIndex = tl.outIndex;
                    if (tl.uploadHeap)
                        localUploadHeaps.push_back(tl.uploadHeap);
                }
                else
                {
                    SDL_Log("Failed to load sky texture: %s, using error texture", path);
                    outIndex = errorIndex;
                }
            }
            else
            {
                outIndex = errorIndex;
            }
        }
        // TODO: Add further object types here (e.g., OBJECT_TERRAIN, OBJECT_DECAL) as needed
    }

    g_engine.pipeline_dx12.m_commandList[0]->Close();
    ID3D12CommandList *ppLists[] = {g_engine.pipeline_dx12.m_commandList[0]};
    g_engine.pipeline_dx12.m_commandQueue->ExecuteCommandLists(1, ppLists);

    WaitForAllFrames();

    for (auto *heap : localUploadHeaps)
        heap->Release();
    localUploadHeaps.clear();
}

int main(void)
{
    program_state.timing.InitTimer();

    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        log_sdl_error("Couldn't initialise SDL");
        return 1;
    }
    else
    {
        SDL_Log("SDL Video initialised.");
    }

    g_liveConfigData = LoadConfig();
    program_state.window.Create();

    if (g_liveConfigData.GraphicsSettings.msaa_level > 1)
    {
        g_engine.msaa_state.m_enabled = true;
        g_engine.msaa_state.m_currentSampleCount = (UINT)g_liveConfigData.GraphicsSettings.msaa_level;
        if (g_engine.msaa_state.m_currentSampleCount == 2)
            g_engine.msaa_state.m_currentSampleIndex = 1;
        else if (g_engine.msaa_state.m_currentSampleCount == 4)
            g_engine.msaa_state.m_currentSampleIndex = 2;
        else if (g_engine.msaa_state.m_currentSampleCount == 8)
            g_engine.msaa_state.m_currentSampleIndex = 3;
    }

    if (!LoadPipeline(program_state.window.hwnd))
    {
        log_error("Could not load pipeline");
        return 1;
    }
    else
    {
        SDL_Log("Pipeline loaded successfully.");
    }

    if (!LoadAssets())
    {
        log_error("Could not load startup assets");
        return 1;
    }
    else
    {
        SDL_Log("Startup assets loaded successfully.");
    }

    // imgui setup
    {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO &io = ImGui::GetIO();
        (void)io;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;  // Enable Gamepad Controls
        ImGui::StyleColorsDark();

        ImGui_ImplSDL3_InitForD3D(program_state.window.window);

        g_imguiHeap.Create(g_engine.pipeline_dx12.m_device, g_engine.pipeline_dx12.m_imguiHeap);
        ImGui_ImplDX12_InitInfo init_info = {};
        init_info.Device = g_engine.pipeline_dx12.m_device;
        init_info.CommandQueue = g_engine.pipeline_dx12.m_commandQueue;
        init_info.NumFramesInFlight = g_FrameCount;
        init_info.RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
        init_info.DSVFormat = DXGI_FORMAT_UNKNOWN;
        init_info.SrvDescriptorHeap = g_imguiHeap.Heap;
        init_info.SrvDescriptorAllocFn = [](ImGui_ImplDX12_InitInfo *, D3D12_CPU_DESCRIPTOR_HANDLE *out_cpu_handle, D3D12_GPU_DESCRIPTOR_HANDLE *out_gpu_handle)
        { return g_imguiHeap.Alloc(out_cpu_handle, out_gpu_handle); };
        init_info.SrvDescriptorFreeFn = [](ImGui_ImplDX12_InitInfo *, D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle, D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle)
        { return g_imguiHeap.Free(cpu_handle, gpu_handle); };
        ImGui_ImplDX12_Init(&init_info);
    }

    read_scene();

    // todo: when we load everything, make a big table that keeps track of everything we have loaded, filenames, objecttypes, and where it is placed
    // todo: do not load same filename more than once

    LoadAllTextures();

    // set up bot objects
    for (int i = 0; i < MAX_BOT_OBJECTS; ++i)
    {
        BotObjects &bot = g_bot_objects[i];

        // Random position within heightfield bounds (approx -100 to 100 in X and Z)
        float x = (float)(rand() % 256) - 0;
        float z = (float)(rand() % 256) - 0;
        float y = 25.0f + (float)(rand() % 20); // between 25 and 45

        bot.pos = {x, y, z};
        bot.scale = {1, 1, 1};

        // Random number of patrol points
        bot.patrolPointCount = 2 + (rand() % (maxPatrolPoints - 2));
        for (int p = 0; p < bot.patrolPointCount && p < maxPatrolPoints; ++p)
        {
            // Each patrol point is a random offset from bot's position
            float offX = (float)(rand() % 60) - 30.0f;
            float offZ = (float)(rand() % 60) - 30.0f;
            float offY = (float)(rand() % 15) - 5.0f; // -5 to +10 from start height
            bot.patrolPoints[p] = {x + offX, y + offY, z + offZ};
        }
        bot.currentPatrolPointTargetIndex = 0;

        // Random mode
        bot.patrolMode = (rand() % 2 == 0) ? PATROL_WRAP : PATROL_REVERSE_DIRECTION;

        // Random speed between 3 and 10
        bot.speed = 3.0f + (float)(rand() % 70) / 10.0f;
        // Random wait time between 1 and 4 seconds
        bot.waitTimeSeconds = 1.0f + (float)(rand() % 30) / 10.0f;

        bot.velocity = {0, 0, 0};
        bot.maxSpeed = 8.0f + (float)(rand() % 50) / 10.0f;     // 8 to 13
        bot.acceleration = 6.0f + (float)(rand() % 40) / 10.0f; // 6 to 10
    }

    for (int i = 0; i < g_scene.objectCount; ++i)
    {
        SceneObject so = g_scene.objects[i];
        if (so.objectType == ObjectType::OBJECT_LOADED_MODEL)
        {
            bool modelAlreadyLoaded = false;
            for (uint32_t j = 0; j < g_engine.graphics_resources.m_numModelsLoaded; ++j)
            {
                if (strcmp(g_modelPaths[j], so.data.loaded_model.pathTo) == 0)
                {
                    // we have already loaded this path
                    g_scene.objects[i].data.loaded_model.model_index = j;
                    modelAlreadyLoaded = true;
                    break;
                }
            }
            if (!modelAlreadyLoaded)
            {
                ModelLoadResult mlr = LoadModelFromFile(so.data.loaded_model.pathTo);
                g_scene.objects[i].data.loaded_model.model_index = mlr.index;
            }
        }
    }

    while (program_state.isRunning)
    {
        SDL_Event sdlEvent;
        while (SDL_PollEvent(&sdlEvent))
        {
            if (g_view_editor)
                ImGui_ImplSDL3_ProcessEvent(&sdlEvent);
            switch (sdlEvent.type)
            {
            case SDL_EVENT_KEY_DOWN:
            {
                if (sdlEvent.key.key == SDLK_F1)
                {
                    g_view_editor = !g_view_editor;
                }
                if (sdlEvent.key.scancode < 512)
                    g_input.keys[sdlEvent.key.scancode] = true;
            }
            break;
            case SDL_EVENT_KEY_UP:
            {
                if (sdlEvent.key.scancode < 512)
                    g_input.keys[sdlEvent.key.scancode] = false;
            }
            break;
            case SDL_EVENT_MOUSE_MOTION:
            {
                if (g_input.mouseCaptured)
                {
                    g_camera.yaw += sdlEvent.motion.xrel * g_camera.lookSpeed;
                    g_camera.pitch -= sdlEvent.motion.yrel * g_camera.lookSpeed;
                    const float maxPitch = DirectX::XMConvertToRadians(89.0f);
                    if (g_camera.pitch > maxPitch)
                        g_camera.pitch = maxPitch;
                    if (g_camera.pitch < -maxPitch)
                        g_camera.pitch = -maxPitch;
                }
            }
            break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            {
                if (sdlEvent.button.button == SDL_BUTTON_LEFT)
                    g_input.lmbDown = true;
            }
            break;
            case SDL_EVENT_MOUSE_BUTTON_UP:
            {
                if (sdlEvent.button.button == SDL_BUTTON_LEFT)
                    g_input.lmbDown = false;
                if (sdlEvent.button.button == SDL_BUTTON_RIGHT && g_input.mouseCaptured)
                {
                    SDL_SetWindowRelativeMouseMode(program_state.window.window, false);
                    g_input.mouseCaptured = false;
                }
            }
            break;
            case SDL_EVENT_QUIT:
            {
                program_state.isRunning = false;
            }
            break;
            }
        }

        if (g_view_editor)
        {
            ImGui_ImplDX12_NewFrame();
            ImGui_ImplSDL3_NewFrame();
            DrawEditorGUI();

            SDL_SetWindowRelativeMouseMode(program_state.window.window, false);
            g_input.mouseCaptured = false;

            // DrawDebugRay();
        }
        else
        {
            SDL_SetWindowRelativeMouseMode(program_state.window.window, true);
            g_input.mouseCaptured = true;
        }

        program_state.timing.UpdateTimer();

        if (window_request.applyWindowRequest)
        {
            program_state.window.ApplyWindowMode();
            window_request.applyWindowRequest = false;
        }

        Update();
        Render((bool)g_liveConfigData.GraphicsSettings.vsync);
        MoveToNextFrame();
    }
    g_imguiHeap.Destroy();
    OnDestroy();
    return (0);
}