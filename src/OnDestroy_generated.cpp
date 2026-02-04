#pragma once
#include "renderer_dx12.h"
#define ONDESTROY_GENERATED_CPP

void OnDestroy()
{
    // Ensure that the GPU is no longer referencing resources that are about to be
    // cleaned up by the destructor.
    WaitForGpu();

    // Release sync objects
    if (sync_state.m_fence)
    {
        sync_state.m_fence->Release();
        sync_state.m_fence = nullptr;
    }

    // Release other resources
    for (UINT i = 0; i < g_FrameCount; i++)
    {
        if (graphics_resources.m_constantBuffer[i])
        {
            graphics_resources.m_constantBuffer[i]->Release();
            graphics_resources.m_constantBuffer[i] = nullptr;
        }
    }

    // Release graphics resources
    if (pipeline_dx12.m_depthStencil)
    {
        pipeline_dx12.m_depthStencil->Release();
        pipeline_dx12.m_depthStencil = nullptr;
    }
    if (graphics_resources.m_vertexBuffer)
    {
        graphics_resources.m_vertexBuffer->Release();
        graphics_resources.m_vertexBuffer = nullptr;
    }
    if (graphics_resources.m_texture)
    {
        graphics_resources.m_texture->Release();
        graphics_resources.m_texture = nullptr;
    }

    // Release pipeline objects
    if (pipeline_dx12.m_commandList)
    {
        pipeline_dx12.m_commandList->Release();
        pipeline_dx12.m_commandList = nullptr;
    }
    if (pipeline_dx12.m_pipelineState)
    {
        pipeline_dx12.m_pipelineState->Release();
        pipeline_dx12.m_pipelineState = nullptr;
    }
    if (pipeline_dx12.m_rootSignature)
    {
        pipeline_dx12.m_rootSignature->Release();
        pipeline_dx12.m_rootSignature = nullptr;
    }

    // Release per-frame resources
    for (UINT i = 0; i < g_FrameCount; i++)
    {
        if (pipeline_dx12.m_renderTargets[i])
        {
            pipeline_dx12.m_renderTargets[i]->Release();
            pipeline_dx12.m_renderTargets[i] = nullptr;
        }
    }
    for (UINT i = 0; i < g_FrameCount; i++)
    {
        if (pipeline_dx12.m_commandAllocators[i])
        {
            pipeline_dx12.m_commandAllocators[i]->Release();
            pipeline_dx12.m_commandAllocators[i] = nullptr;
        }
    }

    // Release descriptor heaps
    if (pipeline_dx12.m_rtvHeap)
    {
        pipeline_dx12.m_rtvHeap->Release();
        pipeline_dx12.m_rtvHeap = nullptr;
    }
    if (pipeline_dx12.m_mainHeap)
    {
        pipeline_dx12.m_mainHeap->Release();
        pipeline_dx12.m_mainHeap = nullptr;
    }
    if (pipeline_dx12.m_dsvHeap)
    {
        pipeline_dx12.m_dsvHeap->Release();
        pipeline_dx12.m_dsvHeap = nullptr;
    }

    // Release swap chain
    if (pipeline_dx12.m_swapChain)
    {
        pipeline_dx12.m_swapChain->Release();
        pipeline_dx12.m_swapChain = nullptr;
    }

    // Release command queue
    if (pipeline_dx12.m_commandQueue)
    {
        pipeline_dx12.m_commandQueue->Release();
        pipeline_dx12.m_commandQueue = nullptr;
    }

    // Release device (last)
    if (pipeline_dx12.m_device)
    {
        pipeline_dx12.m_device->Release();
        pipeline_dx12.m_device = nullptr;
    }

    // Close fence event handle
    if (sync_state.m_fenceEvent)
    {
        CloseHandle(sync_state.m_fenceEvent);
        sync_state.m_fenceEvent = nullptr;
    }
}