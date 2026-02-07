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

#pragma warning(push, 0)
#include <SDL3/SDL.h>
#include <windows.h>
#include <directx/d3d12.h>
#include <dxgi1_6.h>
#include <dxgi1_2.h>
#include <directx/d3dx12.h>

#include <imgui.h>
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_dx12.h>
#pragma warning(pop)

#include "local_error.h"
#include "config_ini_io.h"
#include "renderer_dx12.h"

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

bool PopulateCommandList()
{
    pipeline_dx12.ResetCommandObjects();

    // Common setup (shared by both paths)
    pipeline_dx12.m_commandList->SetGraphicsRootSignature(pipeline_dx12.m_rootSignature);

    ID3D12DescriptorHeap *ppHeaps[] = {pipeline_dx12.m_mainHeap};
    pipeline_dx12.m_commandList->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);

    UINT descriptorSize = pipeline_dx12.m_device->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    CD3DX12_GPU_DESCRIPTOR_HANDLE cbvHandle(
        pipeline_dx12.m_mainHeap->GetGPUDescriptorHandleForHeapStart(),
        (INT)sync_state.m_frameIndex,
        descriptorSize);
    pipeline_dx12.m_commandList->SetGraphicsRootDescriptorTable(0, cbvHandle);

    CD3DX12_GPU_DESCRIPTOR_HANDLE srvHandle(
        pipeline_dx12.m_mainHeap->GetGPUDescriptorHandleForHeapStart(),
        g_FrameCount, // SRV is after all CBVs
        descriptorSize);
    pipeline_dx12.m_commandList->SetGraphicsRootDescriptorTable(1, srvHandle);

    pipeline_dx12.m_commandList->RSSetViewports(1, &pipeline_dx12.m_viewport);
    pipeline_dx12.m_commandList->RSSetScissorRects(1, &pipeline_dx12.m_scissorRect);

    const float clearColour[] = {0.0f, 0.2f, 0.4f, 1.0f};

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
        pipeline_dx12.m_commandList->ResourceBarrier(1, &barrier1);
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
        pipeline_dx12.m_commandList->ResourceBarrier(1, &barrier1);
    }

    // Common rendering operations
    pipeline_dx12.m_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);
    pipeline_dx12.m_commandList->ClearRenderTargetView(rtvHandle, clearColour, 0, nullptr);
    pipeline_dx12.m_commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

    // Draw geometry (same for both)
    pipeline_dx12.m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    pipeline_dx12.m_commandList->IASetVertexBuffers(0, 1, &graphics_resources.m_vertexBufferView);
    pipeline_dx12.m_commandList->DrawInstanced(3, 1, 0, 0);

    // Post-draw operations
    if (msaa_state.m_enabled)
    {
        // MSAA: Resolve to back buffer
        auto barrier2 = CD3DX12_RESOURCE_BARRIER::Transition(pipeline_dx12.m_msaaRenderTargets[sync_state.m_frameIndex], D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
        pipeline_dx12.m_commandList->ResourceBarrier(1, &barrier2);

        pipeline_dx12.m_commandList->ResolveSubresource(pipeline_dx12.m_renderTargets[sync_state.m_frameIndex], 0, pipeline_dx12.m_msaaRenderTargets[sync_state.m_frameIndex], 0, DXGI_FORMAT_R8G8B8A8_UNORM);

        auto barrier3 = CD3DX12_RESOURCE_BARRIER::Transition(pipeline_dx12.m_renderTargets[sync_state.m_frameIndex], D3D12_RESOURCE_STATE_RESOLVE_DEST, D3D12_RESOURCE_STATE_RENDER_TARGET);
        pipeline_dx12.m_commandList->ResourceBarrier(1, &barrier3);

        auto barrier4 = CD3DX12_RESOURCE_BARRIER::Transition(pipeline_dx12.m_msaaRenderTargets[sync_state.m_frameIndex], D3D12_RESOURCE_STATE_RESOLVE_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
        pipeline_dx12.m_commandList->ResourceBarrier(1, &barrier4);
    }

    // ImGui rendering (always to back buffer)
    CD3DX12_CPU_DESCRIPTOR_HANDLE backBufferRtvHandle(
        pipeline_dx12.m_rtvHeap->GetCPUDescriptorHandleForHeapStart(),
        (INT)sync_state.m_frameIndex,
        pipeline_dx12.m_rtvDescriptorSize);
    pipeline_dx12.m_commandList->OMSetRenderTargets(1, &backBufferRtvHandle, FALSE, nullptr);

    ImGui::Render();
    ID3D12DescriptorHeap *imguiHeaps[] = {g_imguiHeap.Heap};
    pipeline_dx12.m_commandList->SetDescriptorHeaps(_countof(imguiHeaps), imguiHeaps);
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), pipeline_dx12.m_commandList);

    // Final transition to PRESENT
    auto finalBarrier = CD3DX12_RESOURCE_BARRIER::Transition(
        pipeline_dx12.m_renderTargets[sync_state.m_frameIndex],
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_PRESENT);
    pipeline_dx12.m_commandList->ResourceBarrier(1, &finalBarrier);

    if (!HRAssert(pipeline_dx12.m_commandList->Close()))
        return false;
    return true;
}

// Update frame-based values.
void Update()
{
    const float translationSpeed = 0.005f;
    const float offsetBounds = 1.25f;

    graphics_resources.m_constantBufferData.offset.x += translationSpeed;
    if (graphics_resources.m_constantBufferData.offset.x > offsetBounds)
    {
        graphics_resources.m_constantBufferData.offset.x = -offsetBounds;
    }
    memcpy(graphics_resources.m_pCbvDataBegin[sync_state.m_frameIndex],
           &graphics_resources.m_constantBufferData,
           sizeof(graphics_resources.m_constantBufferData));
}

// Render the scene.
void Render(bool vsync = true)
{
    // Record all the commands we need to render the scene into the command list.
    if (!PopulateCommandList())
    {
        log_error("A command failed to be populated");
    }

    // Execute the command list.
    ID3D12CommandList *ppCommandLists[] = {pipeline_dx12.m_commandList};
    pipeline_dx12.m_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

    // Present the frame.
    UINT syncInterval = (vsync) ? 1 : 0;
    UINT syncFlags = (vsync) ? 0 : DXGI_PRESENT_ALLOW_TEARING;
    HRAssert(pipeline_dx12.m_swapChain->Present(syncInterval, syncFlags));
    MoveToNextFrame();
}

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

static WindowMode g_defaultWindowMode = WindowMode::WINDOWED;

struct window_state
{
    uint32_t m_windowWidth = 640;
    uint32_t m_windowHeight = 480;
    WindowMode m_currentMode = g_defaultWindowMode;
    WindowMode m_desiredMode = g_defaultWindowMode;
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
        // todo load saved settings from config file
        // m_windowWidth = load from config;
        // m_windowHeight = load from config;
        // m_currentMode = load from config;

        ConfigData winConf = LoadConfig();
        m_windowWidth = (uint32_t)winConf.m_width;
        m_windowHeight = (uint32_t)winConf.height;
        m_currentMode = (WindowMode)winConf.mode;

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
        WindowMode newMode = m_desiredMode;
        SDL_Log("Applying window mode: %d", newMode);

        // Get current display bounds
        SDL_DisplayID display = SDL_GetDisplayForWindow(window);
        SDL_Rect displayBounds;
        SDL_GetDisplayBounds(display, &displayBounds);

        SDL_SetWindowFullscreen(window, (m_desiredMode != WindowMode::WINDOWED));
        SDL_SetWindowResizable(window, false);
        SDL_SetWindowBordered(window, (m_desiredMode == WindowMode::WINDOWED));
        if (newMode == WindowMode::BORDERLESS)
        {
            SDL_SetWindowPosition(window, displayBounds.x, displayBounds.y);
            SDL_SetWindowSize(window, displayBounds.w, displayBounds.h);
        }
        else
        {
            SDL_SetWindowSize(window, (int)m_windowWidth, (int)m_windowHeight);
        }

        // Update viewport state
        int w, h;
        SDL_GetWindowSize(window, &w, &h);
        viewport_state.m_width = (UINT)w;
        viewport_state.m_height = (UINT)h;
        viewport_state.m_aspectRatio = (float)w / (float)h;

        m_currentMode = newMode;
        // m_modeChanged = false;

        SDL_Log("Window mode applied: %dx%d mode=%d", w, h, newMode);

        RecreateSwapChain();

        ConfigData config = {};
        config.m_width = w;
        config.height = h;
        config.mode = (int)newMode;
        SaveConfig(&config);

        return true;
    }
};

static struct
{
    timing_state timing;
    window_state window;

    bool isRunning = true;
} program_state;

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

    program_state.window.Create();

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

        ImGui::Begin("Settings");
        ImGui::Text("Application average %.3f ms/frame (%.2f FPS)",
                    1000.0f / ImGui::GetIO().Framerate,
                    ImGui::GetIO().Framerate);

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
            // Always show 1x (disabled) option
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
            for (UINT i = 1; i < 4; i++) // Start from 1 to skip 1x
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
        }

        ImGui::Separator();

        ImGui::Text("Window Mode");

        static const char *windowModeNames[(const uint32_t)WindowMode::NUM_WINDOW_MODES] = {"Windowed", "Borderless"};
        if (ImGui::BeginCombo("Mode", windowModeNames[(UINT)program_state.window.m_desiredMode]))
        {
            for (int i = 0; i < (const uint32_t)WindowMode::NUM_WINDOW_MODES; i++)
            {
                bool isSelected = (program_state.window.m_desiredMode == (WindowMode)i);
                if (ImGui::Selectable(windowModeNames[i], isSelected))
                {
                    program_state.window.m_desiredMode = (WindowMode)i;
                    // window_state.m_modeChanged = (window_state.m_pendingMode != window_state.m_currentMode);
                }
                if (isSelected)
                {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        ImGui::End();

        program_state.timing.UpdateTimer();

        if (program_state.window.m_currentMode != program_state.window.m_desiredMode)
        {
            program_state.window.ApplyWindowMode();
        }

        Update();
        Render();
    }
    g_imguiHeap.Destroy();
    OnDestroy();
    return (0);
}