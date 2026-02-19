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

#include <imgui.h>
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_dx12.h>
#include <ImGuizmo.h>

#include <cgltf.h>
#include <cJSON.h>
#pragma warning(pop)

#include "local_error.h"
#include "config_ini_io.cpp"
#include "renderer_dx12.cpp"
#include "scene_data.h"
#include "generated/scene_json.cpp"

static ConfigData g_liveConfigData = {};
static Scene g_scene;

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
            viewport_state.m_width = (UINT)w;
            viewport_state.m_height = (UINT)h;
            viewport_state.m_aspectRatio = (float)w / (float)h;
        }
        else
        {
            log_sdl_error("Couldn't create SDL window");
            HRAssert(E_UNEXPECTED);
        }
    }

    // Window mode management functions
    bool ApplyWindowMode()
    {
        WindowMode newMode = window_request.requestedMode;
        SDL_Log("Applying window mode: %d", newMode);

        // Get current display bounds
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
        viewport_state.m_width = (UINT)w;
        viewport_state.m_height = (UINT)h;
        viewport_state.m_aspectRatio = (float)w / (float)h;

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

// basically the game state
static float g_fov_deg = 60.0f;

static bool g_view_editor = true;

// example for next
const int g_draw_list_element_total = 32;
static struct
{
    int drawAmount = g_draw_list_element_total; // this should not be greater than g_draw_list_element_total
    ObjectType objectTypes[g_draw_list_element_total] = {};
    PrimitiveType primitiveTypes[g_draw_list_element_total] = {};
    RenderPipeline pipelines[g_draw_list_element_total] = {};
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
    pipeline_dx12.ResetCommandObjects();

    pipeline_dx12.m_commandList[sync_state.m_frameIndex]->SetGraphicsRootSignature(pipeline_dx12.m_rootSignature);

    ID3D12DescriptorHeap *ppHeaps[] = {pipeline_dx12.m_mainHeap};
    pipeline_dx12.m_commandList[sync_state.m_frameIndex]->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);

    D3D12_GPU_VIRTUAL_ADDRESS cbvAddress = graphics_resources.m_PerFrameConstantBuffer[sync_state.m_frameIndex]->GetGPUVirtualAddress();
    pipeline_dx12.m_commandList[sync_state.m_frameIndex]->SetGraphicsRootConstantBufferView(1, cbvAddress);

    // Set per - scene CBV(root parameter 2 - descriptor table)
    UINT descriptorSize = pipeline_dx12.m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    CD3DX12_GPU_DESCRIPTOR_HANDLE perSceneCbvHandle(
        pipeline_dx12.m_mainHeap->GetGPUDescriptorHandleForHeapStart(),
        DescriptorIndices::PER_SCENE_CBV, // Per-scene CBV is after all per-frame CBVs
        descriptorSize);
    pipeline_dx12.m_commandList[sync_state.m_frameIndex]->SetGraphicsRootDescriptorTable(2, perSceneCbvHandle);

    // texture handle
    CD3DX12_GPU_DESCRIPTOR_HANDLE srvHandle(
        pipeline_dx12.m_mainHeap->GetGPUDescriptorHandleForHeapStart(),
        DescriptorIndices::TEXTURE_SRV, // SRV is after all CBVs
        descriptorSize);
    pipeline_dx12.m_commandList[sync_state.m_frameIndex]->SetGraphicsRootDescriptorTable(3, srvHandle);

    pipeline_dx12.m_commandList[sync_state.m_frameIndex]->RSSetViewports(1, &pipeline_dx12.m_viewport);
    pipeline_dx12.m_commandList[sync_state.m_frameIndex]->RSSetScissorRects(1, &pipeline_dx12.m_scissorRect);

    // Choose RTV and DSV based on MSAA state
    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle;
    CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle;
    ID3D12Resource *renderTarget = nullptr;

    if (msaa_state.m_enabled)
    {
        // MSAA path
        rtvHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(
            pipeline_dx12.m_msaaRtvHeap->GetCPUDescriptorHandleForHeapStart(),
            (INT)sync_state.m_frameIndex,
            pipeline_dx12.m_rtvDescriptorSize);
        dsvHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(
            pipeline_dx12.m_dsvHeap->GetCPUDescriptorHandleForHeapStart());
        dsvHandle.Offset(1, pipeline_dx12.m_dsvDescriptorSize); // MSAA depth at index 1
        renderTarget = pipeline_dx12.m_msaaRenderTargets[sync_state.m_frameIndex];

        // Back buffer starts in PRESENT state for MSAA
        auto barrier1 = CD3DX12_RESOURCE_BARRIER::Transition(
            pipeline_dx12.m_renderTargets[sync_state.m_frameIndex],
            D3D12_RESOURCE_STATE_PRESENT,
            D3D12_RESOURCE_STATE_RESOLVE_DEST);
        pipeline_dx12.m_commandList[sync_state.m_frameIndex]->ResourceBarrier(1, &barrier1);
    }
    else
    {
        // Non-MSAA path
        rtvHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(
            pipeline_dx12.m_rtvHeap->GetCPUDescriptorHandleForHeapStart(),
            (INT)sync_state.m_frameIndex,
            pipeline_dx12.m_rtvDescriptorSize);
        dsvHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(
            pipeline_dx12.m_dsvHeap->GetCPUDescriptorHandleForHeapStart()); // Non-MSAA depth at index 0
        renderTarget = pipeline_dx12.m_renderTargets[sync_state.m_frameIndex];

        // Transition back buffer to RENDER_TARGET
        auto barrier1 = CD3DX12_RESOURCE_BARRIER::Transition(
            renderTarget,
            D3D12_RESOURCE_STATE_PRESENT,
            D3D12_RESOURCE_STATE_RENDER_TARGET);
        pipeline_dx12.m_commandList[sync_state.m_frameIndex]->ResourceBarrier(1, &barrier1);
    }

    // Common rendering operations
    pipeline_dx12.m_commandList[sync_state.m_frameIndex]->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);
    pipeline_dx12.m_commandList[sync_state.m_frameIndex]->ClearRenderTargetView(rtvHandle, g_rtClearValue.Color, 0, nullptr);
    pipeline_dx12.m_commandList[sync_state.m_frameIndex]->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

    // Draw geometry (same for both MSAA)
    pipeline_dx12.m_commandList[sync_state.m_frameIndex]->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    PerDrawRootConstants currentDrawConstants = {};
    for (int i = 0; i < g_draw_list.drawAmount; ++i)
    {
        ObjectType objectType = g_draw_list.objectTypes[i];
        PrimitiveType currentPrimitiveToDraw = g_draw_list.primitiveTypes[i];
        RenderPipeline pl = g_draw_list.pipelines[i];

        UINT psoIndex = msaa_state.m_enabled ? msaa_state.m_currentSampleIndex : 0;
        ID3D12PipelineState *currentPSO = pipeline_dx12.m_pipelineStates[pl][psoIndex];
        if (!currentPSO)
            SDL_Log("ERROR: PSO null for pipeline %d, msaa %d", pl, psoIndex);

        pipeline_dx12.m_commandList[sync_state.m_frameIndex]->SetPipelineState(currentPSO);

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

        pipeline_dx12.m_commandList[sync_state.m_frameIndex]->SetGraphicsRoot32BitConstants(0, sizeof(PerDrawRootConstants) / 4, &currentDrawConstants, 0);

        // pipeline_dx12.m_commandList[sync_state.m_frameIndex]->IASetVertexBuffers(0, 1, &graphics_resources.m_vertexBufferView[currentPrimitiveToDraw]);
        // pipeline_dx12.m_commandList[sync_state.m_frameIndex]->IASetIndexBuffer(&graphics_resources.m_indexBufferView[currentPrimitiveToDraw]);
        // pipeline_dx12.m_commandList[sync_state.m_frameIndex]->DrawIndexedInstanced(graphics_resources.m_indexCount[currentPrimitiveToDraw], 1, 0, 0, 0);

        if (objectType == OBJECT_PRIMITIVE)
        {            
            pipeline_dx12.m_commandList[sync_state.m_frameIndex]->IASetVertexBuffers(0, 1, &graphics_resources.m_vertexBufferView[currentPrimitiveToDraw]);
            pipeline_dx12.m_commandList[sync_state.m_frameIndex]->IASetIndexBuffer(&graphics_resources.m_indexBufferView[currentPrimitiveToDraw]);
            pipeline_dx12.m_commandList[sync_state.m_frameIndex]->DrawIndexedInstanced(graphics_resources.m_indexCount[currentPrimitiveToDraw], 1, 0, 0, 0);
        }
        else if (objectType == OBJECT_HEIGHTFIELD)
        {
            // Use shared heightfield mesh
            pipeline_dx12.m_commandList[sync_state.m_frameIndex]->IASetVertexBuffers(0, 1, &graphics_resources.m_heightfieldVertexView);
            pipeline_dx12.m_commandList[sync_state.m_frameIndex]->IASetIndexBuffer(&graphics_resources.m_heightfieldIndexView);
            pipeline_dx12.m_commandList[sync_state.m_frameIndex]->DrawIndexedInstanced(graphics_resources.m_heightfieldIndexCount, 1, 0, 0, 0);
        }
    }

    // Post-draw operations
    if (msaa_state.m_enabled)
    {
        // MSAA: Resolve to back buffer
        auto barrier2 = CD3DX12_RESOURCE_BARRIER::Transition(pipeline_dx12.m_msaaRenderTargets[sync_state.m_frameIndex], D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
        pipeline_dx12.m_commandList[sync_state.m_frameIndex]->ResourceBarrier(1, &barrier2);

        pipeline_dx12.m_commandList[sync_state.m_frameIndex]->ResolveSubresource(pipeline_dx12.m_renderTargets[sync_state.m_frameIndex], 0, pipeline_dx12.m_msaaRenderTargets[sync_state.m_frameIndex], 0, DXGI_FORMAT_R8G8B8A8_UNORM);

        auto barrier3 = CD3DX12_RESOURCE_BARRIER::Transition(pipeline_dx12.m_renderTargets[sync_state.m_frameIndex], D3D12_RESOURCE_STATE_RESOLVE_DEST, D3D12_RESOURCE_STATE_RENDER_TARGET);
        pipeline_dx12.m_commandList[sync_state.m_frameIndex]->ResourceBarrier(1, &barrier3);

        auto barrier4 = CD3DX12_RESOURCE_BARRIER::Transition(pipeline_dx12.m_msaaRenderTargets[sync_state.m_frameIndex], D3D12_RESOURCE_STATE_RESOLVE_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
        pipeline_dx12.m_commandList[sync_state.m_frameIndex]->ResourceBarrier(1, &barrier4);
    }

    if (g_view_editor)
    {
        // ImGui rendering (always to back buffer)
        CD3DX12_CPU_DESCRIPTOR_HANDLE backBufferRtvHandle(
            pipeline_dx12.m_rtvHeap->GetCPUDescriptorHandleForHeapStart(),
            (INT)sync_state.m_frameIndex,
            pipeline_dx12.m_rtvDescriptorSize);
        pipeline_dx12.m_commandList[sync_state.m_frameIndex]->OMSetRenderTargets(1, &backBufferRtvHandle, FALSE, nullptr);

        ImGui::Render();
        ID3D12DescriptorHeap *imguiHeaps[] = {g_imguiHeap.Heap};
        pipeline_dx12.m_commandList[sync_state.m_frameIndex]->SetDescriptorHeaps(_countof(imguiHeaps), imguiHeaps);
        ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), pipeline_dx12.m_commandList[sync_state.m_frameIndex]);
    }

    // Final transition to PRESENT
    auto finalBarrier = CD3DX12_RESOURCE_BARRIER::Transition(
        pipeline_dx12.m_renderTargets[sync_state.m_frameIndex],
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_PRESENT);
    pipeline_dx12.m_commandList[sync_state.m_frameIndex]->ResourceBarrier(1, &finalBarrier);

    if (!HRAssert(pipeline_dx12.m_commandList[sync_state.m_frameIndex]->Close()))
        return false;
    return true;
}

void Render(bool vsync = true)
{
    if (!PopulateCommandList())
        log_error("A command failed to be populated");

    ID3D12CommandList *ppCommandLists[] = {pipeline_dx12.m_commandList[sync_state.m_frameIndex]};
    pipeline_dx12.m_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

    UINT syncInterval = (vsync) ? 1 : 0;
    UINT syncFlags = (vsync) ? 0 : DXGI_PRESENT_ALLOW_TEARING;
    HRAssert(pipeline_dx12.m_swapChain->Present(syncInterval, syncFlags));
}

// this functions exists for a future where we will do more than just render the whole scene, this will include culling here
void FillDrawList()
{
    int drawCount = 0;
    for (int i = 0; i < g_scene.objectCount && drawCount < g_draw_list_element_total; ++i)
    {
        const SceneObject &obj = g_scene.objects[i];
        if (obj.objectType != OBJECT_PRIMITIVE && obj.objectType != OBJECT_HEIGHTFIELD)
            continue;

        g_draw_list.transforms.pos[drawCount] = obj.pos;
        g_draw_list.transforms.rot[drawCount] = obj.rot;
        g_draw_list.transforms.scale[drawCount] = obj.scale;

        g_draw_list.objectTypes[drawCount] = obj.objectType;

        if (obj.objectType == ObjectType::OBJECT_PRIMITIVE)
        {
            g_draw_list.primitiveTypes[drawCount] = obj.data.primitive.primitiveType;
        }
        else if (obj.objectType == ObjectType::OBJECT_HEIGHTFIELD)
        {
            g_draw_list.primitiveTypes[drawCount] = PrimitiveType::PRIMITIVE_HEIGHTFIELD; // NOTE: not really necessary but may as well set this to heightfield?
            // todo set index of texture (when you load textures build a hash table of of string_path -> loaded texture index???)
        }
        g_draw_list.pipelines[drawCount] = obj.pipeline;
        drawCount++;
    }
    g_draw_list.drawAmount = drawCount;
}

static DirectX::XMMATRIX g_view;
static DirectX::XMMATRIX g_projection;

static struct
{
    bool mouseCaptured = false;
    bool keys[512] = {false};
} g_input;

struct FlyCamera
{
    DirectX::XMFLOAT3 position = {0.0f, 2.0f, -5.0f};
    float yaw = 0.0f;
    float pitch = 0.0f;
    float moveSpeed = 5.0f;
    float lookSpeed = 0.002f;
    float padSpeed = 1.5f;

    void UpdateFlyCamera(float deltaTime)
    {
        DirectX::XMVECTOR forward = DirectX::XMVectorSet(
            sinf(g_camera.yaw) * cosf(g_camera.pitch),
            sinf(g_camera.pitch),
            cosf(g_camera.yaw) * cosf(g_camera.pitch),
            0.0f);
        DirectX::XMVECTOR right = DirectX::XMVector3Cross(DirectX::XMVectorSet(0, 1, 0, 0), forward); // cross(up, forward)
        DirectX::XMVECTOR up = DirectX::XMVectorSet(0, 1, 0, 0);
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
            moveDelta = DirectX::XMVectorScale(moveDelta, deltaTime * g_camera.moveSpeed);
            DirectX::XMVECTOR pos = XMLoadFloat3(&g_camera.position);
            pos = DirectX::XMVectorAdd(pos, moveDelta);
            XMStoreFloat3(&g_camera.position, pos);
        }
    }
} g_camera;

void Update()
{
    FillDrawList();

    // g_input.mouseCaptured = !g_view_editor;
    g_camera.UpdateFlyCamera((float)program_state.timing.deltaTime);

    DirectX::XMVECTOR eye = DirectX::XMLoadFloat3(&g_camera.position);
    DirectX::XMVECTOR forward = DirectX::XMVectorSet(
        sinf(g_camera.yaw) * cosf(g_camera.pitch),
        sinf(g_camera.pitch),
        cosf(g_camera.yaw) * cosf(g_camera.pitch),
        0.0f);
    DirectX::XMVECTOR at = DirectX::XMVectorAdd(eye, forward);
    DirectX::XMVECTOR up = DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    g_view = DirectX::XMMatrixLookAtLH(eye, at, up);
    g_projection = DirectX::XMMatrixPerspectiveFovLH(
        DirectX::XMConvertToRadians(g_fov_deg),
        viewport_state.m_aspectRatio,
        0.01f,
        1000.0f);

    // TRANSPOSE before storing (shader expects column‑major)
    DirectX::XMStoreFloat4x4(&graphics_resources.m_PerFrameConstantBufferData[sync_state.m_frameIndex].view,
                             DirectX::XMMatrixTranspose(g_view));
    DirectX::XMStoreFloat4x4(&graphics_resources.m_PerFrameConstantBufferData[sync_state.m_frameIndex].projection,
                             DirectX::XMMatrixTranspose(g_projection));

    memcpy(graphics_resources.m_pCbvDataBegin[sync_state.m_frameIndex],
           &graphics_resources.m_PerFrameConstantBufferData[sync_state.m_frameIndex],
           sizeof(PerFrameConstantBuffer));
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

void DrawEditorGUI()
{
    ImGui::NewFrame();

    // gizmos
    // ============================================
    // ImGuizmo – using stored quaternion directly
    // ============================================
    ImGuizmo::BeginFrame();
    ImGuizmo::Enable(true);
    ImGuizmo::SetRect(0, 0, (float)viewport_state.m_width, (float)viewport_state.m_height);

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
        float *viewPtr = (float *)&g_view;
        float *projPtr = (float *)&g_projection;
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

    ImGui::Begin("Settings");
    ImGui::SliderFloat("fov_deg", &g_fov_deg, 60.0f, 120.0f);
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

    UINT oldIndex = msaa_state.m_currentSampleIndex;

    // Build the current selection string
    char currentSelection[32];
    if (msaa_state.m_currentSampleIndex == 0)
    {
        snprintf(currentSelection, sizeof(currentSelection), "Disabled (1x)");
    }
    else
    {
        snprintf(currentSelection, sizeof(currentSelection), "%dx MSAA",
                 msaa_state.m_sampleCounts[msaa_state.m_currentSampleIndex]);
    }

    if (ImGui::BeginCombo("Anti-aliasing", currentSelection))
    {
        {
            bool isSelected = (msaa_state.m_currentSampleIndex == 0);
            if (ImGui::Selectable("Disabled (1x)", isSelected))
            {
                msaa_state.m_currentSampleIndex = 0;
                msaa_state.m_currentSampleCount = 1;
                msaa_state.m_enabled = false;
            }
            if (isSelected)
            {
                ImGui::SetItemDefaultFocus();
            }
        }

        // Show MSAA options
        for (UINT i = 1; i < 4; i++)
        {
            bool isSelected = (msaa_state.m_currentSampleIndex == i);
            bool isSupported = msaa_state.m_supported[i];

            char itemLabel[32];
            if (isSupported)
            {
                snprintf(itemLabel, sizeof(itemLabel), "%dx MSAA", msaa_state.m_sampleCounts[i]);
            }
            else
            {
                snprintf(itemLabel, sizeof(itemLabel), "%dx MSAA (unsupported)", msaa_state.m_sampleCounts[i]);
                ImGui::BeginDisabled();
            }

            if (ImGui::Selectable(itemLabel, isSelected) && isSupported)
            {
                msaa_state.m_currentSampleIndex = i;
                msaa_state.m_currentSampleCount = msaa_state.m_sampleCounts[i];
                msaa_state.m_enabled = true;
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
    if (oldIndex != msaa_state.m_currentSampleIndex)
    {
        SDL_Log("MSAA settings changed: %s, %dx",
                msaa_state.m_enabled ? "enabled" : "disabled",
                msaa_state.m_currentSampleCount);
        RecreateMSAAResources();
        if (msaa_state.m_enabled)
        {
            g_liveConfigData.GraphicsSettings.msaa_level = (int)msaa_state.m_currentSampleCount;
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
        graphics_resources.m_PerSceneConstantBufferData.ambient_colour =
            DirectX::XMFLOAT4(g_ambient_color[0], g_ambient_color[1], g_ambient_color[2], 1.0f);

        // Copy to GPU memory
        memcpy(graphics_resources.m_pPerSceneCbvDataBegin,
               &graphics_resources.m_PerSceneConstantBufferData,
               sizeof(graphics_resources.m_PerSceneConstantBufferData));
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

                    // Set sensible defaults for the new type
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
                    case OBJECT_SKY:
                    {
                        strcpy_s(obj.data.sky_sphere.pathToTexture, sizeof(obj.data.sky_sphere.pathToTexture), "");
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

            bool rotationChanged = false;
            rotationChanged |= ImGui::DragFloat("Pitch", &pitchDeg, 0.5f, -180.0f, 180.0f, "%.1f°");
            rotationChanged |= ImGui::DragFloat("Yaw", &yawDeg, 0.5f, -180.0f, 180.0f, "%.1f°");
            rotationChanged |= ImGui::DragFloat("Roll", &rollDeg, 0.5f, -180.0f, 180.0f, "%.1f°");

            if (rotationChanged)
            {
                float p = DirectX::XMConvertToRadians(pitchDeg);
                float y = DirectX::XMConvertToRadians(yawDeg);
                float r = DirectX::XMConvertToRadians(rollDeg);
                DirectX::XMVECTOR Q_ = DirectX::XMQuaternionRotationRollPitchYaw(p, y, r);
                XMStoreFloat4(&obj.rot, Q_);
            }

            ImGui::DragFloat3("Scale", &obj.scale.x, 0.01f, 0.01f, 10.0f);

            // Persist changes
            write_scene();

            ImGui::TreePop();
        }

        ImGui::Separator();
        ImGui::PopID();
    }
    ImGui::End();
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
        msaa_state.m_enabled = true;
        msaa_state.m_currentSampleCount = (UINT)g_liveConfigData.GraphicsSettings.msaa_level;
        if (msaa_state.m_currentSampleCount == 2)
            msaa_state.m_currentSampleIndex = 1;
        else if (msaa_state.m_currentSampleCount == 4)
            msaa_state.m_currentSampleIndex = 2;
        else if (msaa_state.m_currentSampleCount == 8)
            msaa_state.m_currentSampleIndex = 3;
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

    {
        // imgui setup
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO &io = ImGui::GetIO();
        (void)io;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;  // Enable Gamepad Controls
        ImGui::StyleColorsDark();

        ImGui_ImplSDL3_InitForD3D(program_state.window.window);

        g_imguiHeap.Create(pipeline_dx12.m_device, pipeline_dx12.m_imguiHeap);
        ImGui_ImplDX12_InitInfo init_info = {};
        init_info.Device = pipeline_dx12.m_device;
        init_info.CommandQueue = pipeline_dx12.m_commandQueue;
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

    while (program_state.isRunning)
    {
        SDL_Event sdlEvent;
        while (SDL_PollEvent(&sdlEvent))
        {
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
                if (sdlEvent.button.button == SDL_BUTTON_RIGHT && !g_view_editor)
                {
                    SDL_SetWindowRelativeMouseMode(program_state.window.window, true);
                    g_input.mouseCaptured = true;
                }
            }
            break;
            case SDL_EVENT_MOUSE_BUTTON_UP:
            {
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

        ImGui_ImplDX12_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        if (g_view_editor)
            DrawEditorGUI();

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