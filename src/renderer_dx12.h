#pragma once

#pragma warning(push, 0)
#include <windows.h>
#include <directx/d3d12.h>
#include <dxgi1_6.h>
#include <dxgi1_2.h>
#include <directx/d3dx12.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#pragma warning(pop)

#include "local_error.h"

struct Vertex
{
    DirectX::XMFLOAT3 position;
    DirectX::XMFLOAT2 uv;
};

struct ConstantBuffer
{
    DirectX::XMFLOAT4 offset;
    float padding[60]; // Padding so the constant buffer is 256-byte aligned.
};
static_assert((sizeof(ConstantBuffer) % 256) == 0, "Constant Buffer size must be 256-byte aligned");

static DXGI_FORMAT g_screenFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
static D3D12_CLEAR_VALUE g_rtClearValue = {g_screenFormat, {0.0f, 0.2f, 0.4f, 1.0f}};
static D3D12_CLEAR_VALUE g_depthOptimizedClearValue = {DXGI_FORMAT_D32_FLOAT, {1.0f, 0}};

static const UINT g_FrameCount = 3; // double, triple buffering etc...
static struct
{
    UINT64 m_fenceValues[g_FrameCount];
    ID3D12Fence *m_fence;
    HANDLE m_fenceEvent;
    UINT m_frameIndex;
} sync_state;

static struct
{
    bool m_enabled = false;
    UINT m_currentSampleCount = 1;
    UINT m_currentSampleIndex = 0;                     // 0=1x, 1=2x, 2=4x, 3=8x
    bool m_supported[4] = {true, false, false, false}; // 1x always supported
    const UINT m_sampleCounts[4] = {1, 2, 4, 8};

    void CalcSupportedMSAALevels(ID3D12Device *device)
    {
        for (UINT i = 0; i < 4; i++)
        {
            D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS msLevels = {};
            msLevels.Format = g_screenFormat;
            msLevels.SampleCount = msaa_state.m_sampleCounts[i];
            msLevels.Flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE;
            if (SUCCEEDED(device->CheckFeatureSupport(
                    D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &msLevels, sizeof(msLevels))))
            {
                msaa_state.m_supported[i] = (msLevels.NumQualityLevels > 0);
            }
        }
    }
} msaa_state;

static struct
{
    CD3DX12_VIEWPORT m_viewport;
    CD3DX12_RECT m_scissorRect;
    ID3D12Device *m_device;
    ID3D12CommandQueue *m_commandQueue;
    IDXGISwapChain3 *m_swapChain;
    ID3D12DescriptorHeap *m_rtvHeap;
    ID3D12DescriptorHeap *m_mainHeap;
    ID3D12DescriptorHeap *m_imguiHeap;
    ID3D12Resource *m_renderTargets[g_FrameCount];
    ID3D12CommandAllocator *m_commandAllocators[g_FrameCount];
    ID3D12GraphicsCommandList *m_commandList;
    ID3D12RootSignature *m_rootSignature;

    // depth buffer
    ID3D12DescriptorHeap *m_dsvHeap;
    ID3D12Resource *m_depthStencil;

    // MSAA resources
    ID3D12PipelineState *m_pipelineStates[4]; // 1x, 2x, 4x, 8x
    ID3D12DescriptorHeap *m_msaaRtvHeap;
    ID3D12Resource *m_msaaRenderTargets[g_FrameCount];
    ID3D12Resource *m_msaaDepthStencil;

    // descriptor sizes
    UINT m_dsvDescriptorSize;
    UINT m_rtvDescriptorSize;

    void ResetCommandObjects()
    {
        // Command list allocators can only be reset when the associated
        // command lists have finished execution on the GPU; apps should use
        // fences to determine GPU execution progress.
        HRAssert(pipeline_dx12.m_commandAllocators[sync_state.m_frameIndex]->Reset());
        // However, when ExecuteCommandList() is called on a particular command
        // list, that command list can then be reset at any time and must be before
        // re-recording.
        UINT psoIndex = msaa_state.m_enabled ? msaa_state.m_currentSampleIndex : 0;
        HRAssert(pipeline_dx12.m_commandList->Reset(pipeline_dx12.m_commandAllocators[sync_state.m_frameIndex], pipeline_dx12.m_pipelineStates[psoIndex]));
    }
} pipeline_dx12;

static struct
{
    ConstantBuffer m_constantBufferData;
    D3D12_VERTEX_BUFFER_VIEW m_vertexBufferView;
    ID3D12Resource *m_vertexBuffer;
    ID3D12Resource *m_texture;

    ID3D12Resource *m_constantBuffer[g_FrameCount];
    UINT8 *m_pCbvDataBegin[g_FrameCount];
} graphics_resources;

// Wait for pending GPU work to complete.
void WaitForGpu()
{
    // Schedule a Signal command in the queue.
    HRAssert(pipeline_dx12.m_commandQueue->Signal(sync_state.m_fence, sync_state.m_fenceValues[sync_state.m_frameIndex]));
    // Wait until the fence has been processed.
    HRAssert(sync_state.m_fence->SetEventOnCompletion(sync_state.m_fenceValues[sync_state.m_frameIndex], sync_state.m_fenceEvent));
    WaitForSingleObjectEx(sync_state.m_fenceEvent, INFINITE, FALSE);

    // Increment the fence value for the current frame.
    sync_state.m_fenceValues[sync_state.m_frameIndex]++;
}

void WaitForAllFrames()
{
    // Wait for ALL frames to complete (triple buffering)
    UINT64 maxFenceValue = 0;
    
    // Find the maximum fence value among all frames
    for (UINT i = 0; i < g_FrameCount; i++)
    {
        if (sync_state.m_fenceValues[i] > maxFenceValue)
            maxFenceValue = sync_state.m_fenceValues[i];
    }
    
    // Signal the fence with a new value to ensure we wait for all pending work
    const UINT64 currentFenceValue = maxFenceValue + 1;
    HRAssert(pipeline_dx12.m_commandQueue->Signal(sync_state.m_fence, currentFenceValue));
    
    // Wait for the fence to reach the new value
    if (sync_state.m_fence->GetCompletedValue() < currentFenceValue)
    {
        HRAssert(sync_state.m_fence->SetEventOnCompletion(currentFenceValue, sync_state.m_fenceEvent));
        WaitForSingleObjectEx(sync_state.m_fenceEvent, INFINITE, FALSE);
    }
    
    // Update all frame fence values to the new value
    for (UINT i = 0; i < g_FrameCount; i++)
    {
        sync_state.m_fenceValues[i] = currentFenceValue;
    }
}

// Prepare to render the next frame.
void MoveToNextFrame()
{
    // Schedule a Signal command in the queue.
    const UINT64 currentFenceValue = sync_state.m_fenceValues[sync_state.m_frameIndex];
    HRAssert(pipeline_dx12.m_commandQueue->Signal(sync_state.m_fence, currentFenceValue));
    // Update the frame index.
    sync_state.m_frameIndex = pipeline_dx12.m_swapChain->GetCurrentBackBufferIndex();

    // If the next frame is not ready to be rendered yet, wait until it is ready.
    if (sync_state.m_fence->GetCompletedValue() < sync_state.m_fenceValues[sync_state.m_frameIndex])
    {
        HRAssert(sync_state.m_fence->SetEventOnCompletion(sync_state.m_fenceValues[sync_state.m_frameIndex], sync_state.m_fenceEvent));
        WaitForSingleObjectEx(sync_state.m_fenceEvent, INFINITE, FALSE);
    }

    // Set the fence value for the next frame.
    sync_state.m_fenceValues[sync_state.m_frameIndex] = currentFenceValue + 1;
}

static struct
{
    UINT m_width;
    UINT m_height;
    float m_aspectRatio;
} viewport_state;

// Helper function for acquiring the first available hardware adapter that supports Direct3D 12.
// If no such adapter can be found, *ppAdapter will be set to nullptr.
_Use_decl_annotations_ void GetHardwareAdapter(
    IDXGIFactory1 *pFactory,
    IDXGIAdapter1 **ppAdapter,
    bool requestHighPerformanceAdapter)
{
    *ppAdapter = nullptr;

    IDXGIAdapter1 *adapter = nullptr;

    IDXGIFactory6 *factory6;
    if (SUCCEEDED(pFactory->QueryInterface(IID_PPV_ARGS(&factory6))))
    {
        for (
            UINT adapterIndex = 0;
            SUCCEEDED(factory6->EnumAdapterByGpuPreference(
                adapterIndex,
                requestHighPerformanceAdapter == true ? DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE : DXGI_GPU_PREFERENCE_UNSPECIFIED,
                IID_PPV_ARGS(&adapter)));
            ++adapterIndex)
        {
            DXGI_ADAPTER_DESC1 desc;
            adapter->GetDesc1(&desc);

            if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
            {
                // Don't select the Basic Render Driver adapter.
                // If you want a software adapter, pass in "/warp" on the command line.
                continue;
            }

            // Check to see whether the adapter supports Direct3D 12, but don't create the
            // actual device yet.
            if (SUCCEEDED(D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0, _uuidof(ID3D12Device), nullptr)))
            {
                break;
            }
        }
    }

    if (adapter == nullptr)
    {
        for (UINT adapterIndex = 0; SUCCEEDED(pFactory->EnumAdapters1(adapterIndex, &adapter)); ++adapterIndex)
        {
            DXGI_ADAPTER_DESC1 desc;
            adapter->GetDesc1(&desc);

            if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
            {
                // Don't select the Basic Render Driver adapter.
                // If you want a software adapter, pass in "/warp" on the command line.
                continue;
            }

            // Check to see whether the adapter supports Direct3D 12, but don't create the
            // actual device yet.
            if (SUCCEEDED(D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0, _uuidof(ID3D12Device), nullptr)))
            {
                break;
            }
        }
    }

    *ppAdapter = adapter;
}

struct
{
    UINT m_flags = DXGI_ENUM_MODES_SCALING;
    DXGI_FORMAT m_format = g_screenFormat;
    UINT m_numDisplayModes = 0;
    DXGI_MODE_DESC *m_modes = NULL;

    UINT CalcNumberDisplayModes(IDXGIFactory4 *factory)
    {
        UINT totalModes = 0;
        UINT adapterIndex = 0;
        IDXGIAdapter1 *adapter = nullptr;
        while (factory->EnumAdapters1(adapterIndex, &adapter) == S_OK)
        {
            UINT outputIndex = 0;
            IDXGIOutput *output = nullptr;
            while (adapter->EnumOutputs(outputIndex, &output) == S_OK)
            {
                UINT nModes = 0;
                HRESULT hr = output->GetDisplayModeList(
                    m_format,
                    m_flags,
                    &nModes,
                    nullptr);
                HRAssert(hr);
                if (SUCCEEDED(hr))
                    totalModes += nModes;
                output->Release();
                ++outputIndex;
            }
            adapter->Release();
            ++adapterIndex;
        }
        m_numDisplayModes = totalModes;
        return m_numDisplayModes;
    }

    void FillDisplayModesFromFactory(IDXGIFactory4 *factory)
    {
        if (!factory)
        {
            log_error("FillDisplayModesFromFactory: factory is null");
            HRAssert(E_UNEXPECTED);
            return;
        }

        // Allocate memory for all modes
        UINT adapterIndex = 0;
        IDXGIAdapter1 *adapter = nullptr;
        if (CalcNumberDisplayModes(factory) > 0)
        {
            m_modes = (DXGI_MODE_DESC *)SDL_malloc(sizeof(DXGI_MODE_DESC) * m_numDisplayModes);
            if (m_modes)
            {
                m_numDisplayModes = 0;
                adapterIndex = 0;
                while (factory->EnumAdapters1(adapterIndex, &adapter) == S_OK)
                {
                    UINT outputIndex = 0;
                    IDXGIOutput *output = nullptr;
                    while (adapter->EnumOutputs(outputIndex, &output) == S_OK)
                    {
                        UINT nModes = 0;
                        HRESULT hr = output->GetDisplayModeList(m_format, m_flags, &nModes, nullptr);
                        HRAssert(hr);
                        if (SUCCEEDED(hr) && nModes > 0)
                        {
                            hr = output->GetDisplayModeList(m_format, m_flags, &nModes, m_modes + m_numDisplayModes);
                            HRAssert(hr);
                            if (SUCCEEDED(hr))
                            {
                                m_numDisplayModes += nModes;
                            }
                        }
                        output->Release();
                        ++outputIndex;
                    }
                    adapter->Release();
                    ++adapterIndex;
                }
            }
            else
            {
                log_sdl_error("FillDisplayModesFromFactory: malloc failed");
                m_numDisplayModes = 0;
            }
        }
    }

    void PrintDisplayModes()
    {
        SDL_Log("Display modes:");
        for (UINT i = 0; i < m_numDisplayModes; ++i)
        {
            const DXGI_MODE_DESC &m = m_modes[i];
            double hz = (m.RefreshRate.Denominator != 0)
                            ? (double)m.RefreshRate.Numerator / (double)m.RefreshRate.Denominator
                            : 0.0;
            SDL_Log("    Mode %u: %ux%u @ %.2f Hz  Format=%u  Scanline=%u  Scaling=%u",
                    i, m.Width, m.Height, hz,
                    (unsigned)m.Format,
                    (unsigned)m.ScanlineOrdering,
                    (unsigned)m.Scaling);
        }
        if (m_numDisplayModes == 0)
        {
            SDL_Log("    No display modes available");
        }
    }

    void CleanupDisplayModes()
    {
        if (m_modes)
        {
            SDL_free(m_modes);
            m_modes = NULL;
            m_numDisplayModes = 0;
        }
    }
} display_modes;

void CreateMSAAResources(UINT sampleCount)
{
    // Recreate MSAA render targets
    D3D12_RESOURCE_DESC msaaRtDesc = {};
    msaaRtDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    msaaRtDesc.Width = viewport_state.m_width;
    msaaRtDesc.Height = viewport_state.m_height;
    msaaRtDesc.DepthOrArraySize = 1;
    msaaRtDesc.MipLevels = 1;
    msaaRtDesc.Format = g_screenFormat;
    msaaRtDesc.SampleDesc.Count = sampleCount;
    msaaRtDesc.SampleDesc.Quality = 0;
    msaaRtDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    CD3DX12_CPU_DESCRIPTOR_HANDLE msaaRtvHandle(pipeline_dx12.m_msaaRtvHeap->GetCPUDescriptorHandleForHeapStart());
    for (UINT n = 0; n < g_FrameCount; n++)
    {
        HRAssert(pipeline_dx12.m_device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
            D3D12_HEAP_FLAG_NONE,
            &msaaRtDesc,
            D3D12_RESOURCE_STATE_RENDER_TARGET,
            &g_rtClearValue,
            IID_PPV_ARGS(&pipeline_dx12.m_msaaRenderTargets[n])));

        pipeline_dx12.m_device->CreateRenderTargetView(pipeline_dx12.m_msaaRenderTargets[n], nullptr, msaaRtvHandle);
        msaaRtvHandle.Offset(1, pipeline_dx12.m_rtvDescriptorSize);
    }

    // Recreate MSAA depth buffer
    D3D12_RESOURCE_DESC msaaDepthDesc = {};
    msaaDepthDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    msaaDepthDesc.Width = viewport_state.m_width;
    msaaDepthDesc.Height = viewport_state.m_height;
    msaaDepthDesc.DepthOrArraySize = 1;
    msaaDepthDesc.MipLevels = 1;
    msaaDepthDesc.Format = DXGI_FORMAT_D32_FLOAT;
    msaaDepthDesc.SampleDesc.Count = sampleCount;
    msaaDepthDesc.SampleDesc.Quality = 0;
    msaaDepthDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    HRAssert(pipeline_dx12.m_device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE,
        &msaaDepthDesc,
        D3D12_RESOURCE_STATE_DEPTH_WRITE,
        &g_depthOptimizedClearValue,
        IID_PPV_ARGS(&pipeline_dx12.m_msaaDepthStencil)));

    // Create DSV for MSAA depth buffer
    D3D12_DEPTH_STENCIL_VIEW_DESC msaaDsvDesc = {};
    msaaDsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
    msaaDsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DMS;
    msaaDsvDesc.Flags = D3D12_DSV_FLAG_NONE;

    CD3DX12_CPU_DESCRIPTOR_HANDLE msaaDsvHandle(pipeline_dx12.m_dsvHeap->GetCPUDescriptorHandleForHeapStart());
    msaaDsvHandle.Offset(1, pipeline_dx12.m_dsvDescriptorSize);
    pipeline_dx12.m_device->CreateDepthStencilView(
        pipeline_dx12.m_msaaDepthStencil,
        &msaaDsvDesc,
        msaaDsvHandle);
}

void ReleaseMSAAResources()
{
    // Release old MSAA resources
    for (UINT n = 0; n < g_FrameCount; n++)
    {
        if (pipeline_dx12.m_msaaRenderTargets[n])
        {
            pipeline_dx12.m_msaaRenderTargets[n]->Release();
            pipeline_dx12.m_msaaRenderTargets[n] = nullptr;
        }
    }
    if (pipeline_dx12.m_msaaDepthStencil)
    {
        pipeline_dx12.m_msaaDepthStencil->Release();
        pipeline_dx12.m_msaaDepthStencil = nullptr;
    }
}

void RecreateMSAAResources()
{
    WaitForAllFrames();

    ReleaseMSAAResources();

    if (msaa_state.m_enabled)
    {
        CreateMSAAResources(msaa_state.m_currentSampleCount);
    }
}

void RecreateSwapChain(HWND hwnd)
{
    SDL_Log("Recreating swap chain for new window size: %ux%u",
            viewport_state.m_width, viewport_state.m_height);

    // Wait for all GPU work to complete
    WaitForAllFrames();

    // Release old resources
    for (UINT i = 0; i < g_FrameCount; i++)
    {
        if (pipeline_dx12.m_renderTargets[i])
        {
            pipeline_dx12.m_renderTargets[i]->Release();
            pipeline_dx12.m_renderTargets[i] = nullptr;
        }
    }

    if (pipeline_dx12.m_depthStencil)
    {
        pipeline_dx12.m_depthStencil->Release();
        pipeline_dx12.m_depthStencil = nullptr;
    }

    ReleaseMSAAResources();

    // Resize swap chain buffers
    HRESULT hr = pipeline_dx12.m_swapChain->ResizeBuffers(
        g_FrameCount,
        viewport_state.m_width,
        viewport_state.m_height,
        g_screenFormat,
        DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING);

    if (FAILED(hr))
    {
        log_hr_error("Failed to resize swap chain buffers", hr);
        return;
    }

    // Update frame index
    sync_state.m_frameIndex = pipeline_dx12.m_swapChain->GetCurrentBackBufferIndex();

    // Recreate back buffer RTVs
    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(
        pipeline_dx12.m_rtvHeap->GetCPUDescriptorHandleForHeapStart());

    for (UINT i = 0; i < g_FrameCount; i++)
    {
        hr = pipeline_dx12.m_swapChain->GetBuffer(i,
                                                  IID_PPV_ARGS(&pipeline_dx12.m_renderTargets[i]));

        if (FAILED(hr))
        {
            log_hr_error("Failed to get back buffer", hr);
            return;
        }

        pipeline_dx12.m_device->CreateRenderTargetView(
            pipeline_dx12.m_renderTargets[i],
            nullptr,
            rtvHandle);
        rtvHandle.Offset(1, pipeline_dx12.m_rtvDescriptorSize);
    }

    // Recreate depth buffer
    D3D12_RESOURCE_DESC depthStencilDesc = {};
    depthStencilDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    depthStencilDesc.Width = viewport_state.m_width;
    depthStencilDesc.Height = viewport_state.m_height;
    depthStencilDesc.DepthOrArraySize = 1;
    depthStencilDesc.MipLevels = 1;
    depthStencilDesc.Format = DXGI_FORMAT_D32_FLOAT;
    depthStencilDesc.SampleDesc.Count = 1;
    depthStencilDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    hr = pipeline_dx12.m_device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE,
        &depthStencilDesc,
        D3D12_RESOURCE_STATE_DEPTH_WRITE,
        &g_depthOptimizedClearValue,
        IID_PPV_ARGS(&pipeline_dx12.m_depthStencil));

    if (FAILED(hr))
    {
        log_hr_error("Failed to recreate depth buffer", hr);
        return;
    }

    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
    dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    dsvDesc.Flags = D3D12_DSV_FLAG_NONE;

    pipeline_dx12.m_device->CreateDepthStencilView(
        pipeline_dx12.m_depthStencil,
        &dsvDesc,
        pipeline_dx12.m_dsvHeap->GetCPUDescriptorHandleForHeapStart());

    // Update viewport and scissor
    pipeline_dx12.m_viewport = CD3DX12_VIEWPORT(
        0.0f, 0.0f,
        static_cast<float>(viewport_state.m_width),
        static_cast<float>(viewport_state.m_height));

    pipeline_dx12.m_scissorRect = CD3DX12_RECT(
        0, 0,
        static_cast<LONG>(viewport_state.m_width),
        static_cast<LONG>(viewport_state.m_height));

    // Recreate MSAA resources if enabled
    if (msaa_state.m_enabled)
    {
        CreateMSAAResources(msaa_state.m_currentSampleCount);
    }

    SDL_Log("Swap chain successfully recreated");
}

// Load the rendering pipeline dependencies.
bool LoadPipeline(HWND hwnd)
{
    UINT dxgiFactoryFlags = 0;

#if defined(_DEBUG)
    // Enable the debug layer (requires the Graphics Tools "optional feature").
    // NOTE: Enabling the debug layer after device creation will invalidate the active device.
    {
        ID3D12Debug *debugController;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
        {
            debugController->EnableDebugLayer();

            // Enable additional debug layers.
            dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
        }
    }
#endif

    IDXGIFactory4 *factory;
    if (!HRAssert(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&factory))))
        return false;

    // todo move this out
    bool m_useWarpDevice = false;
    if (m_useWarpDevice)
    {
        IDXGIAdapter *warpAdapter;
        if (!HRAssert(factory->EnumWarpAdapter(IID_PPV_ARGS(&warpAdapter))))
            return false;

        if (HRAssert(D3D12CreateDevice(
                warpAdapter,
                D3D_FEATURE_LEVEL_11_0,
                IID_PPV_ARGS(&pipeline_dx12.m_device))))
            return false;
    }
    else
    {
        IDXGIAdapter1 *hardwareAdapter;
        GetHardwareAdapter(factory, &hardwareAdapter, true);

        HRAssert(D3D12CreateDevice(
            hardwareAdapter,
            D3D_FEATURE_LEVEL_11_0,
            IID_PPV_ARGS(&pipeline_dx12.m_device)));
    }

    // Describe and create the command queue.
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

    if (!HRAssert(pipeline_dx12.m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&pipeline_dx12.m_commandQueue))))
        return false;

    // Describe and create the swap chain.
    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
    swapChainDesc.BufferCount = g_FrameCount;
    swapChainDesc.Width = viewport_state.m_width;
    swapChainDesc.Height = viewport_state.m_height;
    swapChainDesc.Format = g_screenFormat;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapChainDesc.SampleDesc.Count = 1;
    swapChainDesc.Flags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;

    IDXGISwapChain1 *swapChain;
    HRAssert(factory->CreateSwapChainForHwnd(
        pipeline_dx12.m_commandQueue, // Swap chain needs the queue so that it can force a flush on it.
        hwnd,
        &swapChainDesc,
        nullptr,
        nullptr,
        &swapChain));

    // This sample does not support fullscreen transitions.
    if (!HRAssert(factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER)))
        return false;

    if (!HRAssert(swapChain->QueryInterface(IID_PPV_ARGS(&pipeline_dx12.m_swapChain))))
        return false;
    sync_state.m_frameIndex = pipeline_dx12.m_swapChain->GetCurrentBackBufferIndex();

    // Create descriptor heaps.
    {
        // Describe and create a render target view (RTV) descriptor heap.
        D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
        rtvHeapDesc.NumDescriptors = g_FrameCount;
        rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        if (!HRAssert(pipeline_dx12.m_device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&pipeline_dx12.m_rtvHeap))))
            return false;

        D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
        dsvHeapDesc.NumDescriptors = 2; // Regular depth + MSAA depth
        dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        if (!HRAssert(pipeline_dx12.m_device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&pipeline_dx12.m_dsvHeap))))
            return false;
        pipeline_dx12.m_dsvDescriptorSize = pipeline_dx12.m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

        // Describe and create a shader resource view (SRV) heap for the texture.
        D3D12_DESCRIPTOR_HEAP_DESC mainHeapDesc = {};
        mainHeapDesc.NumDescriptors = 1 + g_FrameCount;
        mainHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        mainHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (!HRAssert(pipeline_dx12.m_device->CreateDescriptorHeap(&mainHeapDesc, IID_PPV_ARGS(&pipeline_dx12.m_mainHeap))))
            return false;
        pipeline_dx12.m_rtvDescriptorSize = pipeline_dx12.m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

        // Create ImGui's descriptor heap
        D3D12_DESCRIPTOR_HEAP_DESC imguiHeapDesc = {};
        imguiHeapDesc.NumDescriptors = 10; // Enough for fonts and a few textures
        imguiHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        imguiHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (!HRAssert(pipeline_dx12.m_device->CreateDescriptorHeap(&imguiHeapDesc, IID_PPV_ARGS(&pipeline_dx12.m_imguiHeap))))
            return false;

        // Create MSAA RTV descriptor heap
        D3D12_DESCRIPTOR_HEAP_DESC msaaRtvHeapDesc = {};
        msaaRtvHeapDesc.NumDescriptors = g_FrameCount;
        msaaRtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        msaaRtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        if (!HRAssert(pipeline_dx12.m_device->CreateDescriptorHeap(&msaaRtvHeapDesc, IID_PPV_ARGS(&pipeline_dx12.m_msaaRtvHeap))))
            return false;
    }

    pipeline_dx12.m_viewport = CD3DX12_VIEWPORT(0.0f, 0.0f, static_cast<float>(viewport_state.m_width), static_cast<float>(viewport_state.m_height));
    pipeline_dx12.m_scissorRect = CD3DX12_RECT(0, 0, static_cast<LONG>(viewport_state.m_width), static_cast<LONG>(viewport_state.m_height));

    // Create frame resources.
    {
        CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(pipeline_dx12.m_rtvHeap->GetCPUDescriptorHandleForHeapStart());

        // Create a RTV and a command allocator for each frame.
        for (UINT n = 0; n < g_FrameCount; n++)
        {
            if (!HRAssert(pipeline_dx12.m_swapChain->GetBuffer(n, IID_PPV_ARGS(&pipeline_dx12.m_renderTargets[n]))))
                return false;
            pipeline_dx12.m_device->CreateRenderTargetView(pipeline_dx12.m_renderTargets[n], nullptr, rtvHandle);
            rtvHandle.Offset(1, pipeline_dx12.m_rtvDescriptorSize);

            if (!HRAssert(pipeline_dx12.m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&pipeline_dx12.m_commandAllocators[n]))))
                return false;
        }

        // create the depth buffer resource
        D3D12_RESOURCE_DESC depthStencilDesc = {};
        depthStencilDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        depthStencilDesc.Width = viewport_state.m_width;
        depthStencilDesc.Height = viewport_state.m_height;
        depthStencilDesc.DepthOrArraySize = 1;
        depthStencilDesc.MipLevels = 1;
        depthStencilDesc.Format = DXGI_FORMAT_D32_FLOAT;
        depthStencilDesc.SampleDesc.Count = 1;
        depthStencilDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

        pipeline_dx12.m_device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
            D3D12_HEAP_FLAG_NONE,
            &depthStencilDesc,
            D3D12_RESOURCE_STATE_DEPTH_WRITE,
            &g_depthOptimizedClearValue,
            IID_PPV_ARGS(&pipeline_dx12.m_depthStencil));

        D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
        dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
        dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        dsvDesc.Flags = D3D12_DSV_FLAG_NONE;

        pipeline_dx12.m_device->CreateDepthStencilView(
            pipeline_dx12.m_depthStencil,
            &dsvDesc,
            pipeline_dx12.m_dsvHeap->GetCPUDescriptorHandleForHeapStart());

        msaa_state.CalcSupportedMSAALevels(pipeline_dx12.m_device);
        if (msaa_state.m_enabled)
            CreateMSAAResources(msaa_state.m_currentSampleCount);
    }

    display_modes.FillDisplayModesFromFactory(factory);
    display_modes.PrintDisplayModes();
    factory->Release();
    return true;
}

// Generate a simple black and white checkerboard texture.
static const UINT TextureWidth = 256;
static const UINT TextureHeight = 256;
static const UINT TexturePixelSize = 4;
std::vector<UINT8> GenerateTextureData()
{
    const UINT rowPitch = TextureWidth * TexturePixelSize;
    const UINT cellPitch = rowPitch >> 3;      // The width of a cell in the checkboard texture.
    const UINT cellHeight = TextureWidth >> 3; // The height of a cell in the checkerboard texture.
    const UINT textureSize = rowPitch * TextureHeight;

    std::vector<UINT8> data(textureSize);
    UINT8 *pData = &data[0];

    for (UINT n = 0; n < textureSize; n += TexturePixelSize)
    {
        UINT x = n % rowPitch;
        UINT y = n / rowPitch;
        UINT i = x / cellPitch;
        UINT j = y / cellHeight;

        if (i % 2 == j % 2)
        {
            pData[n] = 0x00;     // R
            pData[n + 1] = 0x0f; // G
            pData[n + 2] = 0xff; // B
            pData[n + 3] = 0xff; // A
        }
        else
        {
            pData[n] = 0xff;     // R
            pData[n + 1] = 0xf0; // G
            pData[n + 2] = 0x00; // B
            pData[n + 3] = 0xff; // A
        }
    }

    return data;
}

// Load the startup assets. Returns true on success, false on fail.
bool LoadAssets()
{
    // Create root signature.
    {
        D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};

        // This is the highest version the sample supports. If CheckFeatureSupport succeeds, the HighestVersion returned will not be greater than this.
        featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;

        if (FAILED(pipeline_dx12.m_device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData))))
        {
            featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
        }

        // CD3DX12_DESCRIPTOR_RANGE1 ranges[2];
        // ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);
        // ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);

        CD3DX12_DESCRIPTOR_RANGE1 cbvRange;
        cbvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);
        CD3DX12_DESCRIPTOR_RANGE1 srvRange;
        srvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);

        CD3DX12_ROOT_PARAMETER1 rootParameters[2];
        rootParameters[0].InitAsDescriptorTable(1, &cbvRange, D3D12_SHADER_VISIBILITY_ALL);
        rootParameters[1].InitAsDescriptorTable(1, &srvRange, D3D12_SHADER_VISIBILITY_PIXEL);

        D3D12_STATIC_SAMPLER_DESC sampler = {};
        sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
        sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
        sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
        sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
        sampler.MipLODBias = 0;
        sampler.MaxAnisotropy = 0;
        sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
        sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
        sampler.MinLOD = 0.0f;
        sampler.MaxLOD = D3D12_FLOAT32_MAX;
        sampler.ShaderRegister = 0;
        sampler.RegisterSpace = 0;
        sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
        rootSignatureDesc.Init_1_1(_countof(rootParameters), rootParameters, 1, &sampler, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

        ID3DBlob *signature;
        ID3DBlob *error;
        if (!HRAssert(D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, featureData.HighestVersion, &signature, &error)))
            return false;
        if (!HRAssert(pipeline_dx12.m_device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&pipeline_dx12.m_rootSignature))))
            return false;
    }

    // Create the pipeline states, which includes compiling and loading shaders.
    {
        ID3DBlob *vertexShader;
        ID3DBlob *pixelShader;

#if defined(_DEBUG)
        // Enable better shader debugging with the graphics debugging tools.
        UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
        UINT compileFlags = 0;
#endif

        if (!HRAssert(D3DCompileFromFile(L"shader_source\\shaders.hlsl", nullptr, nullptr, "VSMain", "vs_5_0", compileFlags, 0, &vertexShader, nullptr)))
            return false;
        if (!HRAssert(D3DCompileFromFile(L"shader_source\\shaders.hlsl", nullptr, nullptr, "PSMain", "ps_5_0", compileFlags, 0, &pixelShader, nullptr)))
            return false;
        // Define the vertex input layout.
        D3D12_INPUT_ELEMENT_DESC inputElementDescs[] =
            {
                {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
                {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}};

        // Create PSO for each supported MSAA level
        for (UINT i = 0; i < 4; i++)
        {
            if (!msaa_state.m_supported[i])
            {
                pipeline_dx12.m_pipelineStates[i] = nullptr;
                continue;
            }

            // Describe and create the graphics pipeline state object (PSO).
            D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
            psoDesc.InputLayout = {inputElementDescs, _countof(inputElementDescs)};
            psoDesc.pRootSignature = pipeline_dx12.m_rootSignature;
            psoDesc.VS = CD3DX12_SHADER_BYTECODE(vertexShader);
            psoDesc.PS = CD3DX12_SHADER_BYTECODE(pixelShader);
            psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
            psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
            psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
            psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
            psoDesc.SampleMask = UINT_MAX;
            psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            psoDesc.NumRenderTargets = 1;
            psoDesc.RTVFormats[0] = g_screenFormat;
            psoDesc.SampleDesc.Count = msaa_state.m_sampleCounts[i];
            psoDesc.SampleDesc.Quality = 0;
            if (!HRAssert(pipeline_dx12.m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pipeline_dx12.m_pipelineStates[i]))))
                return false;
        }
    }

    // Create the command list.
    if (!HRAssert(pipeline_dx12.m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, pipeline_dx12.m_commandAllocators[sync_state.m_frameIndex], pipeline_dx12.m_pipelineStates[0], IID_PPV_ARGS(&pipeline_dx12.m_commandList))))
        return false;

    // Create the vertex buffer.
    ID3D12Resource *vertexBufferUpload;
    {
        // Define the geometry for a triangle.
        Vertex triangleVertices[] =
            {
                {{0.0f, 0.25f, 0.0f}, {0.5f, 0.0f}},
                {{0.25f, -0.25f, 0.0f}, {1.0f, 1.0f}},
                {{-0.25f, -0.25f, 0.0f}, {0.0f, 1.0f}}};

        const UINT vertexBufferSize = sizeof(triangleVertices);

        if (!HRAssert(pipeline_dx12.m_device->CreateCommittedResource(
                &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
                D3D12_HEAP_FLAG_NONE,
                &CD3DX12_RESOURCE_DESC::Buffer(vertexBufferSize),
                D3D12_RESOURCE_STATE_COPY_DEST,
                nullptr,
                IID_PPV_ARGS(&graphics_resources.m_vertexBuffer))))
            return false;

        // temp upload heap

        if (!HRAssert(pipeline_dx12.m_device->CreateCommittedResource(
                &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
                D3D12_HEAP_FLAG_NONE,
                &CD3DX12_RESOURCE_DESC::Buffer(vertexBufferSize),
                D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr,
                IID_PPV_ARGS(&vertexBufferUpload))))
            return false;

        // Copy the triangle data to the vertex buffer.
        UINT8 *pVertexDataBegin;
        CD3DX12_RANGE readRange(0, 0); // We do not intend to read from this resource on the CPU.
        if (!HRAssert(vertexBufferUpload->Map(0, &readRange, reinterpret_cast<void **>(&pVertexDataBegin))))
            return false;
        memcpy(pVertexDataBegin, triangleVertices, vertexBufferSize);
        vertexBufferUpload->Unmap(0, nullptr);

        pipeline_dx12.m_commandList->CopyBufferRegion(graphics_resources.m_vertexBuffer, 0, vertexBufferUpload, 0, vertexBufferSize);

        // transition to vertex buffer state
        auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
            graphics_resources.m_vertexBuffer,
            D3D12_RESOURCE_STATE_COPY_DEST,
            D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
        pipeline_dx12.m_commandList->ResourceBarrier(1, &barrier);

        // Initialize the vertex buffer view.
        graphics_resources.m_vertexBufferView.BufferLocation = graphics_resources.m_vertexBuffer->GetGPUVirtualAddress();
        graphics_resources.m_vertexBufferView.StrideInBytes = sizeof(Vertex);
        graphics_resources.m_vertexBufferView.SizeInBytes = vertexBufferSize;
    }

    CD3DX12_CPU_DESCRIPTOR_HANDLE cpuStart(pipeline_dx12.m_mainHeap->GetCPUDescriptorHandleForHeapStart());
    UINT cbvSrvDescriptorSize = pipeline_dx12.m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    // create constant buffer for each frame in the frame buffer
    for (UINT i = 0; i < g_FrameCount; i++)
    {
        const UINT constantBufferSize = sizeof(ConstantBuffer); // CB size is required to be 256-byte aligned.

        if (!HRAssert(pipeline_dx12.m_device->CreateCommittedResource(
                &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
                D3D12_HEAP_FLAG_NONE,
                &CD3DX12_RESOURCE_DESC::Buffer(constantBufferSize),
                D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr,
                IID_PPV_ARGS(&graphics_resources.m_constantBuffer[i]))))
            return false;

        // Describe and create a constant buffer view.
        D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
        cbvDesc.BufferLocation = graphics_resources.m_constantBuffer[i]->GetGPUVirtualAddress();
        cbvDesc.SizeInBytes = constantBufferSize;

        CD3DX12_CPU_DESCRIPTOR_HANDLE cbvHandle(
            cpuStart,
            (INT)i,
            cbvSrvDescriptorSize);
        pipeline_dx12.m_device->CreateConstantBufferView(&cbvDesc, cbvHandle);

        // Map and initialize the constant buffer. We don't unmap this until the
        // app closes. Keeping things mapped for the lifetime of the resource is okay.
        CD3DX12_RANGE readRange(0, 0); // We do not intend to read from this resource on the CPU.
        if (!HRAssert(graphics_resources.m_constantBuffer[i]->Map(0, &readRange, reinterpret_cast<void **>(&graphics_resources.m_pCbvDataBegin[i]))))
            return false;
        memcpy(graphics_resources.m_pCbvDataBegin[i], &graphics_resources.m_constantBufferData, sizeof(graphics_resources.m_constantBufferData));
    }

    // Note: ComPtr's are CPU objects but this resource needs to stay in scope until
    // the command list that references it has finished executing on the GPU.
    // We will flush the GPU at the end of this method to ensure the resource is not
    // prematurely destroyed.
    ID3D12Resource *textureUploadHeap;

    // Create the texture.
    {
        // Describe and create a Texture2D.
        D3D12_RESOURCE_DESC textureDesc = {};
        textureDesc.MipLevels = 1;
        textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        textureDesc.Width = TextureWidth;
        textureDesc.Height = TextureHeight;
        textureDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
        textureDesc.DepthOrArraySize = 1;
        textureDesc.SampleDesc.Count = 1;
        textureDesc.SampleDesc.Quality = 0;
        textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;

        if (!HRAssert(pipeline_dx12.m_device->CreateCommittedResource(
                &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
                D3D12_HEAP_FLAG_NONE,
                &textureDesc,
                D3D12_RESOURCE_STATE_COPY_DEST,
                nullptr,
                IID_PPV_ARGS(&graphics_resources.m_texture))))
            return false;
        const UINT64 uploadBufferSize = GetRequiredIntermediateSize(graphics_resources.m_texture, 0, 1);

        // Create the GPU upload buffer.
        if (!HRAssert(pipeline_dx12.m_device->CreateCommittedResource(
                &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
                D3D12_HEAP_FLAG_NONE,
                &CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize),
                D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr,
                IID_PPV_ARGS(&textureUploadHeap))))
            return false;
        // Copy data to the intermediate upload heap and then schedule a copy
        // from the upload heap to the Texture2D.
        std::vector<UINT8> texture = GenerateTextureData();

        D3D12_SUBRESOURCE_DATA textureData = {};
        textureData.pData = &texture[0];
        textureData.RowPitch = TextureWidth * TexturePixelSize;
        textureData.SlicePitch = textureData.RowPitch * TextureHeight;

        UpdateSubresources(pipeline_dx12.m_commandList, graphics_resources.m_texture, textureUploadHeap, 0, 0, 1, &textureData);
        auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(graphics_resources.m_texture, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        pipeline_dx12.m_commandList->ResourceBarrier(1, &barrier);

        UINT increment = pipeline_dx12.m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        CD3DX12_CPU_DESCRIPTOR_HANDLE cpuSrv(cpuStart);
        cpuSrv.Offset(g_FrameCount, increment);

        // Describe and create a SRV for the texture.
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Format = textureDesc.Format;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = 1;
        pipeline_dx12.m_device->CreateShaderResourceView(graphics_resources.m_texture, &srvDesc, cpuSrv);
    }

    // Close the command list and execute it to begin the initial GPU setup.
    if (!HRAssert(pipeline_dx12.m_commandList->Close()))
        return false;
    ID3D12CommandList *ppCommandLists[] = {pipeline_dx12.m_commandList};
    pipeline_dx12.m_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

    // Create synchronization objects and wait until assets have been uploaded to the GPU.
    {
        if (!HRAssert(pipeline_dx12.m_device->CreateFence(sync_state.m_fenceValues[sync_state.m_frameIndex], D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&sync_state.m_fence))))
            return false;
        sync_state.m_fenceValues[sync_state.m_frameIndex]++;

        // Create an event handle to use for frame synchronization.
        sync_state.m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        if (sync_state.m_fenceEvent == nullptr)
        {
            if (!HRAssert(HRESULT_FROM_WIN32(GetLastError())))
                return false;
        }

        // Wait for the command list to execute; we are reusing the same command
        // list in our main loop but for now, we just want to wait for setup to
        // complete before continuing.
        WaitForGpu();
    }
    vertexBufferUpload->Release();
    textureUploadHeap->Release();
    return true;
}

#include "OnDestroy_generated.cpp"
#ifndef ONDESTROY_GENERATED_CPP
void OnDestroy()
{
    // Ensure that the GPU is no longer referencing resources that are about to be
    // cleaned up by the destructor.
    WaitForGpu();

    CloseHandle(sync_state.m_fenceEvent);
}
#endif