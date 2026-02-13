#pragma warning(disable : 5045) // disabling the spectre mitigation warning (not relevant because we are a game, no sensitive information should be in this program)
#pragma warning(disable : 4238) // nonstandard lvalue as rvalue warning
#pragma warning(disable : 4820) // padding warnings
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
#else
#pragma comment(lib, "DirectXTex_release.lib")
#pragma comment(lib, "release/ImGuizmo.lib")
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
#pragma warning(pop)

#include "local_error.h"
#include "config_ini_io.h"
#include "renderer_dx12.h"

static ConfigData g_liveConfigData = {};

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
static float g_r = 0.7f;
static float g_y = 0.0f;
static float g_theta = 0.7f;
static float g_fov_deg = 60.0f;
static PrimitiveType g_viewPrimitive = PrimitiveType::PRIMITIVE_CUBE;

// example for next
const int g_draw_list_element_total = 32;
static struct
{
    int drawAmount = g_draw_list_element_total; // this should not be greater than g_draw_list_element_total
    PrimitiveType types[g_draw_list_element_total] = {};
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
        PrimitiveType currentPrimitiveToDraw = g_draw_list.types[i];

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

        pipeline_dx12.m_commandList[sync_state.m_frameIndex]->IASetVertexBuffers(0, 1, &graphics_resources.m_vertexBufferView[currentPrimitiveToDraw]);
        pipeline_dx12.m_commandList[sync_state.m_frameIndex]->SetGraphicsRoot32BitConstants(0, sizeof(PerDrawRootConstants) / 4, &currentDrawConstants, 0);
        pipeline_dx12.m_commandList[sync_state.m_frameIndex]->IASetIndexBuffer(&graphics_resources.m_indexBufferView[currentPrimitiveToDraw]);
        pipeline_dx12.m_commandList[sync_state.m_frameIndex]->DrawIndexedInstanced(graphics_resources.m_indexCount[currentPrimitiveToDraw], 1, 0, 0, 0);
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

// Render the scene.
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

// todo change this to a "scene" patter
static const int g_total_objects_count = 16;
static struct
{
    // todo: add ambient colour
    // add one skybox set    
    struct
    {
        DirectX::XMFLOAT3 pos;
        DirectX::XMFLOAT4 rot = {0, 0, 0, 1}; // quaternion (default identity)
        DirectX::XMFLOAT3 scale = {1, 1, 1};  // default {1,1,1}
        PrimitiveType type = PrimitiveType::PRIMITIVE_CUBE;
    } objects[g_total_objects_count];

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
} g_total_objects_list_editable;

void FillDrawList()
{
    for (int i = 0; i < g_draw_list_element_total; ++i)
    {
        if (i < g_total_objects_count)
        {
            g_draw_list.transforms.pos[i].x = g_total_objects_list_editable.objects[i].pos.x;
            g_draw_list.transforms.pos[i].y = g_total_objects_list_editable.objects[i].pos.y;
            g_draw_list.transforms.pos[i].z = g_total_objects_list_editable.objects[i].pos.z;

            g_draw_list.transforms.rot[i] = g_total_objects_list_editable.objects[i].rot;

            g_draw_list.transforms.scale[i].x = g_total_objects_list_editable.objects[i].scale.x;
            g_draw_list.transforms.scale[i].y = g_total_objects_list_editable.objects[i].scale.y;
            g_draw_list.transforms.scale[i].z = g_total_objects_list_editable.objects[i].scale.z;

            g_draw_list.types[i] = g_total_objects_list_editable.objects[i].type;
        }
    }
    if (g_total_objects_count < g_draw_list_element_total)
        g_draw_list.drawAmount = g_total_objects_count;
    else
        g_draw_list.drawAmount = g_draw_list_element_total;
}

void Update()
{
    FillDrawList();

    DirectX::XMVECTOR eye = DirectX::XMVectorSet(g_r * sinf(g_theta), g_y, g_r * cosf(g_theta), 0.0f);
    // DirectX::XMVECTOR eye = DirectX::XMVectorSet(0, g_y, g_r, 0.0f);
    DirectX::XMVECTOR at = DirectX::XMVectorSet(0.0f, 0.0f, 0.0f, 0.0f);
    DirectX::XMVECTOR up = DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    DirectX::XMMATRIX view = DirectX::XMMatrixLookAtLH(eye, at, up);

    DirectX::XMMATRIX projection = DirectX::XMMatrixPerspectiveFovLH(
        DirectX::XMConvertToRadians(g_fov_deg),
        viewport_state.m_aspectRatio,
        0.01f,
        1000.0f);

    // TRANSPOSE before storing because dx shader does not use row major by default
    DirectX::XMStoreFloat4x4(&graphics_resources.m_PerFrameConstantBufferData[sync_state.m_frameIndex].view, DirectX::XMMatrixTranspose(view));
    DirectX::XMStoreFloat4x4(&graphics_resources.m_PerFrameConstantBufferData[sync_state.m_frameIndex].projection, DirectX::XMMatrixTranspose(projection));

    memcpy(graphics_resources.m_pCbvDataBegin[sync_state.m_frameIndex],
           &graphics_resources.m_PerFrameConstantBufferData[sync_state.m_frameIndex],
           sizeof(PerFrameConstantBuffer));
}

// Convert quaternion → pitch/yaw/roll (radians), order: pitch (X), yaw (Y), roll (Z)
inline void QuaternionToEuler(DirectX::FXMVECTOR Q, float &pitch, float &yaw, float &roll)
{
    DirectX::XMFLOAT4 q;
    DirectX::XMStoreFloat4(&q, Q);
    float x = q.x, y = q.y, z = q.z, w = q.w;

    // pitch (x-axis rotation)
    float sinp = 2.0f * (w * x + y * z);
    float cosp = 1.0f - 2.0f * (x * x + y * y);
    pitch = atan2f(sinp, cosp);

    // yaw (y-axis rotation)
    float siny = 2.0f * (w * y - z * x);
    if (fabsf(siny) >= 1.0f)
        yaw = copysignf(3.14159265f / 2.0f, siny);
    else
        yaw = asinf(siny);

    // roll (z-axis rotation)
    float sinr = 2.0f * (w * z + x * y);
    float cosr = 1.0f - 2.0f * (y * y + z * z);
    roll = atan2f(sinr, cosr);
}

// editor state
static int g_selectedObjectIndex = 0;

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

    g_total_objects_list_editable.read();

    while (program_state.isRunning)
    {
        SDL_Event sdlEvent;
        while (SDL_PollEvent(&sdlEvent))
        {
            ImGui_ImplSDL3_ProcessEvent(&sdlEvent);
            switch (sdlEvent.type)
            {
            case SDL_EVENT_QUIT:
            {
                program_state.isRunning = false;
            }
            break;
            }
        }
        ImGui_ImplDX12_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        // gizmos
        // ============================================
        // ImGuizmo – using stored quaternion directly
        // ============================================
        ImGuizmo::BeginFrame();
        ImGuizmo::Enable(true);
        ImGuizmo::SetRect(0, 0, (float)viewport_state.m_width, (float)viewport_state.m_height);

        if (g_selectedObjectIndex >= 0 && g_selectedObjectIndex < g_total_objects_count)
        {
            auto &obj = g_total_objects_list_editable.objects[g_selectedObjectIndex];

            // ---- Build world matrix from pos, quaternion, scale (row‑major) ----
            DirectX::XMMATRIX scale = DirectX::XMMatrixScaling(obj.scale.x, obj.scale.y, obj.scale.z);
            DirectX::XMVECTOR rotQuat = DirectX::XMLoadFloat4(&obj.rot);
            DirectX::XMMATRIX rotation = DirectX::XMMatrixRotationQuaternion(rotQuat);
            DirectX::XMMATRIX translation = DirectX::XMMatrixTranslation(obj.pos.x, obj.pos.y, obj.pos.z);
            DirectX::XMMATRIX world = scale * rotation * translation; // row‑major

            // ---- Compute view/proj directly from current camera (row‑major) ----
            DirectX::XMVECTOR eye = DirectX::XMVectorSet(g_r * sinf(g_theta), g_y, g_r * cosf(g_theta), 0.0f);
            DirectX::XMVECTOR at = DirectX::XMVectorSet(0.0f, 0.0f, 0.0f, 0.0f);
            DirectX::XMVECTOR up = DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
            DirectX::XMMATRIX view = DirectX::XMMatrixLookAtLH(eye, at, up); // row‑major
            DirectX::XMMATRIX proj = DirectX::XMMatrixPerspectiveFovLH(
                DirectX::XMConvertToRadians(g_fov_deg),
                viewport_state.m_aspectRatio,
                0.01f, 1000.0f); // row‑major

            // ---- ImGuizmo expects row‑major float[4][4] – pass directly ----
            float *viewPtr = (float *)&view;
            float *projPtr = (float *)&proj;
            float *worldPtr = (float *)&world;

            // ---- Draw gizmo ----
            ImGuizmo::Manipulate(viewPtr, projPtr,
                                 ImGuizmo::OPERATION::TRANSLATE | ImGuizmo::OPERATION::ROTATE | ImGuizmo::OPERATION::SCALE,
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
                g_total_objects_list_editable.write();
            }
        }

        ImGui::Begin("Settings");
        ImGui::SliderFloat("r", &g_r, 0.3f, 10.0f);
        ImGui::SliderFloat("y", &g_y, -10.0f, 10.0f);
        ImGui::SliderFloat("theta", &g_theta, 0.0f, 2 * 3.14159f);        
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

        static float g_ambient_color[3] = {0.0f, 0.0f, 0.0f};
        ImGui::Separator();
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

        ImGui::Separator();
        ImGui::Text("Primitive");

        int currentPrimitive = (int)g_viewPrimitive;
        if (ImGui::Combo("Shape", &currentPrimitive, g_primitiveNames, IM_ARRAYSIZE(g_primitiveNames)))
        {
            g_viewPrimitive = (PrimitiveType)currentPrimitive;
        }

        ImGui::End();

        // ============================================
        // Scene Objects Editor – editing g_total_objects_list_editable
        // ============================================

        ImGui::Begin("Scene Objects");
        ImGui::Text("Total objects: %d", g_total_objects_count);

        for (int i = 0; i < g_total_objects_count; ++i)
        {
            ImGui::PushID(i);
            auto &obj = g_total_objects_list_editable.objects[i];

            // Header label
            char headerLabel[64];
            snprintf(headerLabel, sizeof(headerLabel), "Object %d: %s",
                     i, g_primitiveNames[obj.type]);

            // --- TreeNode with selection support ---
            ImGuiTreeNodeFlags node_flags = ImGuiTreeNodeFlags_OpenOnArrow |
                                            ImGuiTreeNodeFlags_OpenOnDoubleClick |
                                            ImGuiTreeNodeFlags_SpanAvailWidth;
            if (i == g_selectedObjectIndex)
                node_flags |= ImGuiTreeNodeFlags_Selected;

            bool node_open = ImGui::TreeNodeEx(headerLabel, node_flags);

            // --- Click selection (only if not toggling open/close) ---
            if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
            {
                g_selectedObjectIndex = i;
            }

            if (node_open)
            {
                // ---- Primitive type dropdown ----
                int currentType = obj.type;
                if (ImGui::Combo("Primitive", &currentType,
                                 g_primitiveNames, IM_ARRAYSIZE(g_primitiveNames)))
                {
                    obj.type = (PrimitiveType)currentType;
                }

                ImGui::DragFloat3("Position", &obj.pos.x, 0.1f);

                // ---- Rotation (quaternion → Euler sliders) ----
                DirectX::XMFLOAT4 q = obj.rot;
                DirectX::XMVECTOR Q = XMLoadFloat4(&q);
                float pitch, yaw, roll;
                QuaternionToEuler(Q, pitch, yaw, roll);
                float pitchDeg = DirectX::XMConvertToDegrees(pitch);
                float yawDeg = DirectX::XMConvertToDegrees(yaw);
                float rollDeg = DirectX::XMConvertToDegrees(roll);

                ImGui::DragFloat("Pitch", &pitchDeg, 0.5f, -180, 180, "%.1f°");
                ImGui::DragFloat("Yaw", &yawDeg, 0.5f, -180, 180, "%.1f°");
                ImGui::DragFloat("Roll", &rollDeg, 0.5f, -180, 180, "%.1f°");

                if (ImGui::IsItemDeactivatedAfterEdit())
                {
                    float p = DirectX::XMConvertToRadians(pitchDeg);
                    float y = DirectX::XMConvertToRadians(yawDeg);
                    float r = DirectX::XMConvertToRadians(rollDeg);
                    DirectX::XMVECTOR Q_ = DirectX::XMQuaternionRotationRollPitchYaw(p, y, r);
                    XMStoreFloat4(&obj.rot, Q_);
                }

                ImGui::DragFloat3("Scale", &obj.scale.x, 0.01f, 0.01f, 10.0f);

                // Persist changes
                g_total_objects_list_editable.write();

                ImGui::TreePop();
            }

            ImGui::Separator();
            ImGui::PopID();
        }

        ImGui::End();

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