#pragma warning(disable : 5045) // disabling the spectre mitigation warning (not relevant because we are a game, no sensitive information should be in this program)
#pragma comment(lib, "SDL3.lib")
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "dxcompiler.lib")
#pragma comment(lib, "dxguid.lib")

#pragma comment(lib, "user32.lib")

#pragma warning(push, 0)
#include <SDL3/SDL.h>
#include <windows.h>
#include <directx/d3d12.h>
#include <dxgi1_6.h>
#include <dxgi1_2.h>
#include <directx/d3dx12.h>
#pragma warning(pop)

#include "local_error.h"
#include "renderer_dx12.h"

bool PopulateCommandList()
{
    // Command list allocators can only be reset when the associated
    // command lists have finished execution on the GPU; apps should use
    // fences to determine GPU execution progress.
    if (!HRAssert(pipeline_dx12.m_commandAllocator->Reset()))
        return false;
    // However, when ExecuteCommandList() is called on a particular command
    // list, that command list can then be reset at any time and must be before
    // re-recording.
    if (!HRAssert(pipeline_dx12.m_commandList->Reset(pipeline_dx12.m_commandAllocator, pipeline_dx12.m_pipelineState)))
        return false;

    // Set necessary state.
    pipeline_dx12.m_commandList->SetGraphicsRootSignature(pipeline_dx12.m_rootSignature);
    pipeline_dx12.m_commandList->RSSetViewports(1, &pipeline_dx12.m_viewport);
    pipeline_dx12.m_commandList->RSSetScissorRects(1, &pipeline_dx12.m_scissorRect);

    // Indicate that the back buffer will be used as a render target.
    {
        auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(pipeline_dx12.m_renderTargets[sync_state.m_frameIndex], D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
        pipeline_dx12.m_commandList->ResourceBarrier(1, &barrier);
    }

    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(pipeline_dx12.m_rtvHeap->GetCPUDescriptorHandleForHeapStart(), (INT)sync_state.m_frameIndex, pipeline_dx12.m_rtvDescriptorSize);

    pipeline_dx12.m_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

    // Record commands.
    const float clearColor[] = {0.0f, 0.2f, 0.4f, 1.0f};
    pipeline_dx12.m_commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
    pipeline_dx12.m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    pipeline_dx12.m_commandList->IASetVertexBuffers(0, 1, &graphics_resources.m_vertexBufferView);
    pipeline_dx12.m_commandList->DrawInstanced(3, 1, 0, 0);

    // Indicate that the back buffer will now be used to present.
    {
        auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(pipeline_dx12.m_renderTargets[sync_state.m_frameIndex], D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
        pipeline_dx12.m_commandList->ResourceBarrier(1, &barrier);
    }

    if (!HRAssert(pipeline_dx12.m_commandList->Close()))
        return false;
    return true;
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
    WaitForPreviousFrame();
}

struct timing_state
{
    Uint64 lastCounter = 0;
    double upTime = 0.0;
    double deltaTime = 0.0;
    uint32_t frames = 0;

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
        frames++;
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

    program_state.window = SDL_CreateWindow("Name", 640, 480, SDL_WINDOW_RESIZABLE);
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

    while (program_state.isRunning)
    {
        SDL_Event sdlEvent;
        while (SDL_PollEvent(&sdlEvent))
        {
            switch (sdlEvent.type)
            {
            case SDL_EVENT_QUIT:
            {
                program_state.isRunning = false;
            }
            break;
            case SDL_EVENT_WINDOW_RESIZED:
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            {
                int w, h;
                SDL_GetWindowSizeInPixels(program_state.window, &w, &h);
                OnResize((UINT)w, (UINT)h);
            }
            break;
            }
        }

        program_state.timing.UpdateTimer();
        program_state.DisplayPerformanceInWindowTitle(0.1);

        Render();
    }

    OnDestroy();
    return (0);
}