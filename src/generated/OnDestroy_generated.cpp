//------------------------------------------------------------------------
// GENERATED ONDESTROY – DO NOT EDIT
//   This file was automatically generated.
//   by meta_ondestroy.py
//   Generated: 2026-02-24 06:43:51
//------------------------------------------------------------------------

#pragma once
#include "renderer_dx12.cpp"
#define ONDESTROY_GENERATED_CPP

void OnDestroy()
{
    // Ensure that the GPU is no longer referencing resources that are about to be
    // cleaned up by the destructor.
    WaitForGpu();

    // Unmap and release constant buffers
    if (graphics_resources.m_PerSceneConstantBuffer)
    {
        graphics_resources.m_PerSceneConstantBuffer->Unmap(0, nullptr);
        graphics_resources.m_PerSceneConstantBuffer->Release();
        graphics_resources.m_PerSceneConstantBuffer = nullptr;
            graphics_resources.m_pPerSceneCbvDataBegin = nullptr;
    }
    for (UINT i = 0; i < g_FrameCount; i++)
    {
        if (graphics_resources.m_PerFrameConstantBuffer[i])
        {
            graphics_resources.m_PerFrameConstantBuffer[i]->Unmap(0, nullptr);
            graphics_resources.m_PerFrameConstantBuffer[i]->Release();
            graphics_resources.m_PerFrameConstantBuffer[i] = nullptr;
            graphics_resources.m_pCbvDataBegin[i] = nullptr;
        }
    }

    // Release resources inside arrays of structs
    for (UINT i = 0; i < MAX_LOADED_MODELS; i++)
    {
        if (graphics_resources.m_models[i].vertexBuffer)
        {
            graphics_resources.m_models[i].vertexBuffer->Release();
            graphics_resources.m_models[i].vertexBuffer = nullptr;
        }
        if (graphics_resources.m_models[i].indexBuffer)
        {
            graphics_resources.m_models[i].indexBuffer->Release();
            graphics_resources.m_models[i].indexBuffer = nullptr;
        }
    }

    // Release sync objects
    if (sync_state.m_fence)
    {
        sync_state.m_fence->Release();
        sync_state.m_fence = nullptr;
    }

    // Release graphics resources
    if (pipeline_dx12.m_depthStencil)
    {
        pipeline_dx12.m_depthStencil->Release();
        pipeline_dx12.m_depthStencil = nullptr;
    }
    for (UINT i = 0; i < PrimitiveType::PRIMITIVE_COUNT; i++)
    {
        if (graphics_resources.m_vertexBuffer[i])
        {
            graphics_resources.m_vertexBuffer[i]->Release();
            graphics_resources.m_vertexBuffer[i] = nullptr;
        }
    }

    // Release pipeline objects
    for (UINT i = 0; i < g_FrameCount; i++)
    {
        if (pipeline_dx12.m_commandList[i])
        {
            pipeline_dx12.m_commandList[i]->Release();
            pipeline_dx12.m_commandList[i] = nullptr;
        }
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

    // Release other resources
    if (pipeline_dx12.m_imguiHeap)
    {
        pipeline_dx12.m_imguiHeap->Release();
        pipeline_dx12.m_imguiHeap = nullptr;
    }
    if (pipeline_dx12.m_msaaRtvHeap)
    {
        pipeline_dx12.m_msaaRtvHeap->Release();
        pipeline_dx12.m_msaaRtvHeap = nullptr;
    }
    for (UINT i = 0; i < g_FrameCount; i++)
    {
        if (pipeline_dx12.m_msaaRenderTargets[i])
        {
            pipeline_dx12.m_msaaRenderTargets[i]->Release();
            pipeline_dx12.m_msaaRenderTargets[i] = nullptr;
        }
    }
    if (pipeline_dx12.m_msaaDepthStencil)
    {
        pipeline_dx12.m_msaaDepthStencil->Release();
        pipeline_dx12.m_msaaDepthStencil = nullptr;
    }

    // Release graphics resources
    for (UINT i = 0; i < PrimitiveType::PRIMITIVE_COUNT; i++)
    {
        if (graphics_resources.m_indexBuffer[i])
        {
            graphics_resources.m_indexBuffer[i]->Release();
            graphics_resources.m_indexBuffer[i] = nullptr;
        }
    }

    // Release other resources
    if (graphics_resources.m_albedoTexture)
    {
        graphics_resources.m_albedoTexture->Release();
        graphics_resources.m_albedoTexture = nullptr;
    }
    if (graphics_resources.m_heightmapTexture)
    {
        graphics_resources.m_heightmapTexture->Release();
        graphics_resources.m_heightmapTexture = nullptr;
    }
    if (graphics_resources.m_heightfieldVertexBuffer)
    {
        graphics_resources.m_heightfieldVertexBuffer->Release();
        graphics_resources.m_heightfieldVertexBuffer = nullptr;
    }
    if (graphics_resources.m_heightfieldIndexBuffer)
    {
        graphics_resources.m_heightfieldIndexBuffer->Release();
        graphics_resources.m_heightfieldIndexBuffer = nullptr;
    }
    if (graphics_resources.m_heightfieldVertexUpload)
    {
        graphics_resources.m_heightfieldVertexUpload->Release();
        graphics_resources.m_heightfieldVertexUpload = nullptr;
    }
    if (graphics_resources.m_heightfieldIndexUpload)
    {
        graphics_resources.m_heightfieldIndexUpload->Release();
        graphics_resources.m_heightfieldIndexUpload = nullptr;
    }
    for (UINT i = 0; i < MAX_HEIGHTMAP_TEXTURES; i++)
    {
        if (graphics_resources.m_heightmapResources[i])
        {
            graphics_resources.m_heightmapResources[i]->Release();
            graphics_resources.m_heightmapResources[i] = nullptr;
        }
    }
    for (UINT i = 0; i < MAX_SKY_TEXTURES; i++)
    {
        if (graphics_resources.m_skyResources[i])
        {
            graphics_resources.m_skyResources[i]->Release();
            graphics_resources.m_skyResources[i] = nullptr;
        }
    }

    // Close fence event handle
    if (sync_state.m_fenceEvent)
    {
        CloseHandle(sync_state.m_fenceEvent);
        sync_state.m_fenceEvent = nullptr;
    }
}