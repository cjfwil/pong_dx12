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

#include "src/local_error.h"
#include "src/renderer_dx12.h"

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

    // Set necessary state.
    pipeline_dx12.m_commandList->SetGraphicsRootSignature(pipeline_dx12.m_rootSignature);

    ID3D12DescriptorHeap *ppHeaps[] = {pipeline_dx12.m_mainHeap};
    pipeline_dx12.m_commandList->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);

    UINT descriptorSize = pipeline_dx12.m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
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

    if (msaa_state.m_enabled)
    {
        // MSAA path: render to MSAA RT, then resolve to back buffer
        CD3DX12_CPU_DESCRIPTOR_HANDLE msaaRtvHandle(pipeline_dx12.m_msaaRtvHeap->GetCPUDescriptorHandleForHeapStart(), (INT)sync_state.m_frameIndex, pipeline_dx12.m_rtvDescriptorSize);
        CD3DX12_CPU_DESCRIPTOR_HANDLE msaaDsvHandle(pipeline_dx12.m_dsvHeap->GetCPUDescriptorHandleForHeapStart());
        msaaDsvHandle.Offset(1, pipeline_dx12.m_dsvDescriptorSize); // MSAA depth is at index 1

        pipeline_dx12.m_commandList->OMSetRenderTargets(1, &msaaRtvHandle, FALSE, &msaaDsvHandle);
        pipeline_dx12.m_commandList->ClearRenderTargetView(msaaRtvHandle, clearColour, 0, nullptr);
        pipeline_dx12.m_commandList->ClearDepthStencilView(msaaDsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

        // Draw geometry to MSAA RT
        pipeline_dx12.m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        pipeline_dx12.m_commandList->IASetVertexBuffers(0, 1, &graphics_resources.m_vertexBufferView);
        pipeline_dx12.m_commandList->DrawInstanced(3, 1, 0, 0);

        // Transition back buffer to render target for resolve
        {
            auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(pipeline_dx12.m_renderTargets[sync_state.m_frameIndex], D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RESOLVE_DEST);
            pipeline_dx12.m_commandList->ResourceBarrier(1, &barrier);
        }

        // Transition MSAA RT to resolve source
        {
            auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(pipeline_dx12.m_msaaRenderTargets[sync_state.m_frameIndex], D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
            pipeline_dx12.m_commandList->ResourceBarrier(1, &barrier);
        }

        // Resolve MSAA RT to back buffer
        pipeline_dx12.m_commandList->ResolveSubresource(
            pipeline_dx12.m_renderTargets[sync_state.m_frameIndex], 0,
            pipeline_dx12.m_msaaRenderTargets[sync_state.m_frameIndex], 0,
            DXGI_FORMAT_R8G8B8A8_UNORM);

        // Transition back buffer to render target for ImGui
        {
            auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(pipeline_dx12.m_renderTargets[sync_state.m_frameIndex], D3D12_RESOURCE_STATE_RESOLVE_DEST, D3D12_RESOURCE_STATE_RENDER_TARGET);
            pipeline_dx12.m_commandList->ResourceBarrier(1, &barrier);
        }

        // Transition MSAA RT back to render target for next frame
        {
            auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(pipeline_dx12.m_msaaRenderTargets[sync_state.m_frameIndex], D3D12_RESOURCE_STATE_RESOLVE_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
            pipeline_dx12.m_commandList->ResourceBarrier(1, &barrier);
        }
    }
    else
    {
        // Non-MSAA path: render directly to back buffer
        {
            auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(pipeline_dx12.m_renderTargets[sync_state.m_frameIndex], D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
            pipeline_dx12.m_commandList->ResourceBarrier(1, &barrier);
        }

        CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(pipeline_dx12.m_rtvHeap->GetCPUDescriptorHandleForHeapStart(), (INT)sync_state.m_frameIndex, pipeline_dx12.m_rtvDescriptorSize);
        CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(pipeline_dx12.m_dsvHeap->GetCPUDescriptorHandleForHeapStart());

        pipeline_dx12.m_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);
        pipeline_dx12.m_commandList->ClearRenderTargetView(rtvHandle, clearColour, 0, nullptr);
        pipeline_dx12.m_commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

        // Draw geometry
        pipeline_dx12.m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        pipeline_dx12.m_commandList->IASetVertexBuffers(0, 1, &graphics_resources.m_vertexBufferView);
        pipeline_dx12.m_commandList->DrawInstanced(3, 1, 0, 0);
    }

    // ImGui always renders to back buffer (non-MSAA)
    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(pipeline_dx12.m_rtvHeap->GetCPUDescriptorHandleForHeapStart(), (INT)sync_state.m_frameIndex, pipeline_dx12.m_rtvDescriptorSize);
    pipeline_dx12.m_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

    ImGui::Render();
    ID3D12DescriptorHeap *imguiHeaps[] = {g_imguiHeap.Heap};
    pipeline_dx12.m_commandList->SetDescriptorHeaps(_countof(imguiHeaps), imguiHeaps);
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), pipeline_dx12.m_commandList);

    // Indicate that the back buffer will now be used to present.
    {
        auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(pipeline_dx12.m_renderTargets[sync_state.m_frameIndex], D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
        pipeline_dx12.m_commandList->ResourceBarrier(1, &barrier);
    }

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

static struct
{
    timing_state timing;

    SDL_Window *window = nullptr;
    HWND hwnd = nullptr;
    bool isRunning = true;

    void DisplayPerformanceInWindowTitle(double rate)
    {
        static double local_timer = 0.0;
        if (local_timer >= rate)
        {
            double fps = 1.0 / timing.deltaTime;

            char title[128];
            snprintf(title, sizeof(title),
                     "Performance  |  %.2f ms  |  %.1f FPS",
                     timing.deltaTime * 1000.0, fps);

            SDL_SetWindowTitle(window, title);

            local_timer = 0.0;
        }
        local_timer += timing.deltaTime;
    }

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

    program_state.window = SDL_CreateWindow("Name", 1920, 1080, SDL_WINDOW_BORDERLESS);
    if (program_state.window == nullptr)
    {
        log_sdl_error("Couldn't create SDL window");
        return 1;
    }
    else
    {
        SDL_Log("SDL Window created.");
        program_state.GetWindowHWND();

        int w = 0, h = 0;
        SDL_GetWindowSize(program_state.window, &w, &h);
        viewport_state.m_width = (UINT)w;
        viewport_state.m_height = (UINT)h;
        viewport_state.m_aspectRatio = (float)w / (float)h;
    }

    if (!LoadPipeline(program_state.hwnd))
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

        ImGui_ImplSDL3_InitForD3D(program_state.window);

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

        ImGui::Text("Application average %.3f ms/frame (%.2f FPS)",
                    1000.0f / ImGui::GetIO().Framerate,
                    ImGui::GetIO().Framerate);

        // MSAA settings
        ImGui::Separator();
        ImGui::Text("MSAA Settings");

        bool msaaChanged = false;
        bool oldEnabled = msaa_state.m_enabled;
        UINT oldIndex = msaa_state.m_currentSampleIndex;

        if (ImGui::Checkbox("Enable MSAA", &msaa_state.m_enabled))
        {
            msaaChanged = (oldEnabled != msaa_state.m_enabled);
        }

        if (msaa_state.m_enabled)
        {
            ImGui::Text("Sample Count:");
            for (UINT i = 1; i < 4; i++) // Skip 1x (index 0)
            {
                if (msaa_state.m_supported[i])
                {
                    char label[32];
                    snprintf(label, sizeof(label), "%dx MSAA", msaa_state.m_sampleCounts[i]);
                    if (ImGui::RadioButton(label, (int*)&msaa_state.m_currentSampleIndex, (int)i))
                    {
                        msaaChanged = (oldIndex != msaa_state.m_currentSampleIndex);
                        msaa_state.m_currentSampleCount = msaa_state.m_sampleCounts[msaa_state.m_currentSampleIndex];
                    }
                }
                else
                {
                    char label[32];
                    snprintf(label, sizeof(label), "%dx MSAA (unsupported)", msaa_state.m_sampleCounts[i]);
                    ImGui::TextDisabled("%s", label);
                }
            }
        }

        // Handle MSAA changes
        if (msaaChanged)
        {
            SDL_Log("MSAA settings changed: %s, %dx",
                    msaa_state.m_enabled ? "enabled" : "disabled",
                    msaa_state.m_currentSampleCount);
            RecreateMSAAResources();
        }

        ImGui::Separator();

        program_state.timing.UpdateTimer();
        program_state.DisplayPerformanceInWindowTitle(0.1);

        Update();
        Render();
    }

    g_imguiHeap.Destroy();
    OnDestroy();
    return (0);
}