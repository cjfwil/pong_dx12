//------------------------------------------------------------------------
// GENERATED ONDESTROY – DO NOT EDIT
//   This file was automatically generated.
//   by meta_ondestroy.py
//   Generated: 2026-02-25 13:43:01
//------------------------------------------------------------------------

#pragma once
#include "renderer_dx12.cpp"
#define ONDESTROY_GENERATED_CPP

void OnDestroy()
{
    // Ensure that the GPU is no longer referencing resources that are about to be
    // cleaned up by the destructor.
    WaitForGpu();
    WaitForAllFrames();

    // Unmap and release constant buffers
    if (g_engine.graphics_resources.m_PerSceneConstantBuffer)
    {
        g_engine.graphics_resources.m_PerSceneConstantBuffer->Unmap(0, nullptr);
        g_engine.graphics_resources.m_PerSceneConstantBuffer->Release();
        g_engine.graphics_resources.m_PerSceneConstantBuffer = nullptr;
            g_engine.graphics_resources.m_pPerSceneCbvDataBegin = nullptr;
    }
    for (UINT i = 0; i < g_FrameCount; i++)
    {
        if (g_engine.graphics_resources.m_PerFrameConstantBuffer[i])
        {
            g_engine.graphics_resources.m_PerFrameConstantBuffer[i]->Unmap(0, nullptr);
            g_engine.graphics_resources.m_PerFrameConstantBuffer[i]->Release();
            g_engine.graphics_resources.m_PerFrameConstantBuffer[i] = nullptr;
            g_engine.graphics_resources.m_pCbvDataBegin[i] = nullptr;
        }
    }

    // Release resources inside arrays of structs
    for (UINT i = 0; i < MAX_LOADED_MODELS; i++)
    {
        if (g_engine.graphics_resources.m_models[i].vertexBuffer)
        {
            g_engine.graphics_resources.m_models[i].vertexBuffer->Release();
            g_engine.graphics_resources.m_models[i].vertexBuffer = nullptr;
        }
        if (g_engine.graphics_resources.m_models[i].indexBuffer)
        {
            g_engine.graphics_resources.m_models[i].indexBuffer->Release();
            g_engine.graphics_resources.m_models[i].indexBuffer = nullptr;
        }
    }

    // Release sync objects
    if (g_engine.sync_state.m_fence)
    {
        g_engine.sync_state.m_fence->Release();
        g_engine.sync_state.m_fence = nullptr;
    }

    // Release graphics resources
    if (g_engine.pipeline_dx12.m_depthStencil)
    {
        g_engine.pipeline_dx12.m_depthStencil->Release();
        g_engine.pipeline_dx12.m_depthStencil = nullptr;
    }
    for (UINT i = 0; i < PrimitiveType::PRIMITIVE_COUNT; i++)
    {
        if (g_engine.graphics_resources.m_vertexBuffer[i])
        {
            g_engine.graphics_resources.m_vertexBuffer[i]->Release();
            g_engine.graphics_resources.m_vertexBuffer[i] = nullptr;
        }
    }

    // Release pipeline objects
    for (UINT i = 0; i < g_FrameCount; i++)
    {
        if (g_engine.pipeline_dx12.m_commandList[i])
        {
            g_engine.pipeline_dx12.m_commandList[i]->Release();
            g_engine.pipeline_dx12.m_commandList[i] = nullptr;
        }
    }
    if (g_engine.pipeline_dx12.m_rootSignature)
    {
        g_engine.pipeline_dx12.m_rootSignature->Release();
        g_engine.pipeline_dx12.m_rootSignature = nullptr;
    }

    // Release per-frame resources
    for (UINT i = 0; i < g_FrameCount; i++)
    {
        if (g_engine.pipeline_dx12.m_renderTargets[i])
        {
            g_engine.pipeline_dx12.m_renderTargets[i]->Release();
            g_engine.pipeline_dx12.m_renderTargets[i] = nullptr;
        }
    }

    // Release descriptor heaps
    if (g_engine.pipeline_dx12.m_rtvHeap)
    {
        g_engine.pipeline_dx12.m_rtvHeap->Release();
        g_engine.pipeline_dx12.m_rtvHeap = nullptr;
    }
    if (g_engine.pipeline_dx12.m_mainHeap)
    {
        g_engine.pipeline_dx12.m_mainHeap->Release();
        g_engine.pipeline_dx12.m_mainHeap = nullptr;
    }
    if (g_engine.pipeline_dx12.m_dsvHeap)
    {
        g_engine.pipeline_dx12.m_dsvHeap->Release();
        g_engine.pipeline_dx12.m_dsvHeap = nullptr;
    }

    // Release swap chain
    if (g_engine.pipeline_dx12.m_swapChain)
    {
        g_engine.pipeline_dx12.m_swapChain->Release();
        g_engine.pipeline_dx12.m_swapChain = nullptr;
    }

    // Release command queue
    if (g_engine.pipeline_dx12.m_commandQueue)
    {
        g_engine.pipeline_dx12.m_commandQueue->Release();
        g_engine.pipeline_dx12.m_commandQueue = nullptr;
    }

    // Release device (last)
    if (g_engine.pipeline_dx12.m_device)
    {
        g_engine.pipeline_dx12.m_device->Release();
        g_engine.pipeline_dx12.m_device = nullptr;
    }

    // Release per-frame resources
    for (UINT i = 0; i < g_FrameCount; i++)
    {
        if (g_engine.pipeline_dx12.m_commandAllocators[i])
        {
            g_engine.pipeline_dx12.m_commandAllocators[i]->Release();
            g_engine.pipeline_dx12.m_commandAllocators[i] = nullptr;
        }
    }

    // Release other resources
    if (g_engine.pipeline_dx12.m_imguiHeap)
    {
        g_engine.pipeline_dx12.m_imguiHeap->Release();
        g_engine.pipeline_dx12.m_imguiHeap = nullptr;
    }
    if (g_engine.pipeline_dx12.m_msaaRtvHeap)
    {
        g_engine.pipeline_dx12.m_msaaRtvHeap->Release();
        g_engine.pipeline_dx12.m_msaaRtvHeap = nullptr;
    }
    for (UINT i = 0; i < g_FrameCount; i++)
    {
        if (g_engine.pipeline_dx12.m_msaaRenderTargets[i])
        {
            g_engine.pipeline_dx12.m_msaaRenderTargets[i]->Release();
            g_engine.pipeline_dx12.m_msaaRenderTargets[i] = nullptr;
        }
    }
    if (g_engine.pipeline_dx12.m_msaaDepthStencil)
    {
        g_engine.pipeline_dx12.m_msaaDepthStencil->Release();
        g_engine.pipeline_dx12.m_msaaDepthStencil = nullptr;
    }

    // Release graphics resources
    for (UINT i = 0; i < PrimitiveType::PRIMITIVE_COUNT; i++)
    {
        if (g_engine.graphics_resources.m_indexBuffer[i])
        {
            g_engine.graphics_resources.m_indexBuffer[i]->Release();
            g_engine.graphics_resources.m_indexBuffer[i] = nullptr;
        }
    }

    // Release other resources
    if (g_engine.graphics_resources.m_defaultTexture)
    {
        g_engine.graphics_resources.m_defaultTexture->Release();
        g_engine.graphics_resources.m_defaultTexture = nullptr;
    }
    if (g_engine.graphics_resources.m_heightmapTexture)
    {
        g_engine.graphics_resources.m_heightmapTexture->Release();
        g_engine.graphics_resources.m_heightmapTexture = nullptr;
    }
    if (g_engine.graphics_resources.m_heightfieldVertexBuffer)
    {
        g_engine.graphics_resources.m_heightfieldVertexBuffer->Release();
        g_engine.graphics_resources.m_heightfieldVertexBuffer = nullptr;
    }
    if (g_engine.graphics_resources.m_heightfieldIndexBuffer)
    {
        g_engine.graphics_resources.m_heightfieldIndexBuffer->Release();
        g_engine.graphics_resources.m_heightfieldIndexBuffer = nullptr;
    }
    if (g_engine.graphics_resources.m_heightfieldVertexUpload)
    {
        g_engine.graphics_resources.m_heightfieldVertexUpload->Release();
        g_engine.graphics_resources.m_heightfieldVertexUpload = nullptr;
    }
    if (g_engine.graphics_resources.m_heightfieldIndexUpload)
    {
        g_engine.graphics_resources.m_heightfieldIndexUpload->Release();
        g_engine.graphics_resources.m_heightfieldIndexUpload = nullptr;
    }
    for (UINT i = 0; i < MAX_HEIGHTMAP_TEXTURES; i++)
    {
        if (g_engine.graphics_resources.m_heightmapResources[i])
        {
            g_engine.graphics_resources.m_heightmapResources[i]->Release();
            g_engine.graphics_resources.m_heightmapResources[i] = nullptr;
        }
    }
    for (UINT i = 0; i < MAX_SKY_TEXTURES; i++)
    {
        if (g_engine.graphics_resources.m_skyResources[i])
        {
            g_engine.graphics_resources.m_skyResources[i]->Release();
            g_engine.graphics_resources.m_skyResources[i] = nullptr;
        }
    }
    for (UINT i = 0; i < MAX_LOADED_MODELS; i++)
    {
        if (g_engine.graphics_resources.m_modelAlbedoTextures[i])
        {
            g_engine.graphics_resources.m_modelAlbedoTextures[i]->Release();
            g_engine.graphics_resources.m_modelAlbedoTextures[i] = nullptr;
        }
    }

    // Close fence event handle
    if (g_engine.sync_state.m_fenceEvent)
    {
        CloseHandle(g_engine.sync_state.m_fenceEvent);
        g_engine.sync_state.m_fenceEvent = nullptr;
    }
}