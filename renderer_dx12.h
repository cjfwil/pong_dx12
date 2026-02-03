#pragma once

#pragma warning(push, 0)
#include <windows.h>
#include <directx/d3d12.h>
#include <dxgi1_6.h>
#include <dxgi1_2.h>
#include <directx/d3dx12.h>
#pragma warning(pop)

#include "local_error.h"

static struct
{
    static const UINT FrameCount = 2; // double, triple buffering etc...
    ID3D12Device *m_device;
    ID3D12CommandQueue *m_commandQueue;
    IDXGISwapChain3 *m_swapChain;
    ID3D12DescriptorHeap *m_rtvHeap;
    ID3D12Resource *m_renderTargets[FrameCount];
    ID3D12CommandAllocator *m_commandAllocator;
    ID3D12GraphicsCommandList *m_commandList;
    ID3D12PipelineState* m_pipelineState;
    UINT m_rtvDescriptorSize;
} pipeline_dx12;

static struct
{
    UINT64 m_fenceValue;
    ID3D12Fence *m_fence;
    HANDLE m_fenceEvent;
    UINT m_frameIndex;
} sync_state;

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
    swapChainDesc.BufferCount = pipeline_dx12.FrameCount;
    swapChainDesc.Width = viewport_state.m_width;
    swapChainDesc.Height = viewport_state.m_height;
    swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
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
        rtvHeapDesc.NumDescriptors = pipeline_dx12.FrameCount;
        rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        if (!HRAssert(pipeline_dx12.m_device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&pipeline_dx12.m_rtvHeap))))
            return false;

        pipeline_dx12.m_rtvDescriptorSize = pipeline_dx12.m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    }

    // Create frame resources.
    {
        CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(pipeline_dx12.m_rtvHeap->GetCPUDescriptorHandleForHeapStart());

        // Create a RTV for each frame.
        for (UINT n = 0; n < pipeline_dx12.FrameCount; n++)
        {
            if (!HRAssert(pipeline_dx12.m_swapChain->GetBuffer(n, IID_PPV_ARGS(&pipeline_dx12.m_renderTargets[n]))))
                return false;
            pipeline_dx12.m_device->CreateRenderTargetView(pipeline_dx12.m_renderTargets[n], nullptr, rtvHandle);
            rtvHandle.Offset(1, pipeline_dx12.m_rtvDescriptorSize);
        }
    }

    if (!HRAssert(pipeline_dx12.m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&pipeline_dx12.m_commandAllocator))))
        return false;

    return true;
}

// Load the startup assets. Returns true on success, false on fail.
bool LoadAssets()
{
    // Create the command list.
    if (!HRAssert(pipeline_dx12.m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, pipeline_dx12.m_commandAllocator, nullptr, IID_PPV_ARGS(&pipeline_dx12.m_commandList))))
        return false;
    // Command lists are created in the recording state, but there is nothing
    // to record yet. The main loop expects it to be closed, so close it now.
    if (!HRAssert(pipeline_dx12.m_commandList->Close()))
        return false;
    // Create synchronization objects.
    {
        if (!HRAssert(pipeline_dx12.m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&sync_state.m_fence))))
            return false;
        sync_state.m_fenceValue = 1;

        // Create an event handle to use for frame synchronization.
        sync_state.m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        if (sync_state.m_fenceEvent == nullptr)
        {
            if (!HRAssert(HRESULT_FROM_WIN32(GetLastError())))
                return false;
        }
    }
    return true;
}
