#pragma once

#pragma warning(push, 0)
#include <windows.h>
#include <directx/d3d12.h>
#include <dxgi1_6.h>
#include <dxgi1_2.h>
#include <directx/d3dx12.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <DirectXTex.h>
#include <SDL3/SDL.h>
#pragma warning(pop)

#include "local_error.h"

struct Vertex
{
    DirectX::XMFLOAT3 position;
    DirectX::XMFLOAT3 normal;
    DirectX::XMFLOAT2 uv;
};
#include "mesh_data.h"
#include "render_pipeline_data.h"
#include "scene_data.h"

static ID3D12Resource *g_vertexBufferUploadPrimitives[PrimitiveType::PRIMITIVE_COUNT] = {};
static ID3D12Resource *g_indexBufferUploadPrimitives[PrimitiveType::PRIMITIVE_COUNT] = {};
static UINT g_cbvSrvDescriptorSize = 0;

bool CreateDefaultBuffer(
    ID3D12Device *device,
    ID3D12GraphicsCommandList *cmdList,
    const void *data,
    UINT64 size,
    D3D12_RESOURCE_STATES targetState,
    ID3D12Resource **outResource,
    ID3D12Resource **outUploadBuffer) // caller must release after GPU work
{
    HRESULT hr = device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Buffer(size),
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(outResource));
    if (FAILED(hr))
        return false;

    hr = device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
        D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Buffer(size),
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(outUploadBuffer));
    if (FAILED(hr))
        return false;

    void *mapped;
    hr = (*outUploadBuffer)->Map(0, nullptr, &mapped);
    if (FAILED(hr))
        return false;
    memcpy(mapped, data, size);
    (*outUploadBuffer)->Unmap(0, nullptr);

    cmdList->CopyBufferRegion(*outResource, 0, *outUploadBuffer, 0, size);

    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        *outResource,
        D3D12_RESOURCE_STATE_COPY_DEST,
        targetState);
    cmdList->ResourceBarrier(1, &barrier);

    return true;
}

bool CreatePrimitiveMeshBuffers(
    ID3D12Device *device,
    ID3D12GraphicsCommandList *cmdList,
    PrimitiveType type,
    const PrimitiveMeshData &data,
    ID3D12Resource *&outVertexBuffer,
    D3D12_VERTEX_BUFFER_VIEW &outVertexView,
    ID3D12Resource *&outIndexBuffer,
    D3D12_INDEX_BUFFER_VIEW &outIndexView,
    UINT &outIndexCount)
{
    // --- Vertex buffer ---
    const UINT vbSize = data.vertexCount * sizeof(Vertex);
    if (!CreateDefaultBuffer(device, cmdList, data.vertices, vbSize, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER, &outVertexBuffer, &g_vertexBufferUploadPrimitives[type]))
        return false;

    // --- Index buffer ---
    const UINT ibSize = data.indexCount * sizeof(uint32_t);
    if (!CreateDefaultBuffer(device, cmdList, data.indices, ibSize, D3D12_RESOURCE_STATE_INDEX_BUFFER, &outIndexBuffer, &g_indexBufferUploadPrimitives[type]))
        return false;

    // --- Views ---
    outVertexView.BufferLocation = outVertexBuffer->GetGPUVirtualAddress();
    outVertexView.StrideInBytes = sizeof(Vertex);
    outVertexView.SizeInBytes = vbSize;

    outIndexView.BufferLocation = outIndexBuffer->GetGPUVirtualAddress();
    outIndexView.SizeInBytes = ibSize;
    outIndexView.Format = DXGI_FORMAT_R32_UINT;

    outIndexCount = data.indexCount;

    return true;
}

struct PerFrameConstantBuffer
{
    DirectX::XMFLOAT4X4 view;
    DirectX::XMFLOAT4X4 projection;
    float padding[16 + 16]; // Padding so the constant buffer is 256-byte aligned.
};
static_assert((sizeof(PerFrameConstantBuffer) % 256) == 0, "Per Frame Constant Buffer size must be 256-byte aligned");

struct PerSceneConstantBuffer
{
    DirectX::XMFLOAT4 ambient_colour;
    DirectX::XMFLOAT4 light_direction;
    DirectX::XMFLOAT4 light_colour;
    float padding[52];
};
static_assert((sizeof(PerSceneConstantBuffer) % 256) == 0, " Per Scene Constant Buffer size must be 256-byte aligned");

static DXGI_FORMAT g_screenFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
static D3D12_CLEAR_VALUE g_rtClearValue = {g_screenFormat, {0.0f, 0.2f, 0.4f, 1.0f}};
static D3D12_CLEAR_VALUE g_depthOptimizedClearValue = {DXGI_FORMAT_D32_FLOAT, {0.0f, 0}};

static constexpr UINT g_FrameCount = 3; // double, triple buffering etc...

// #define MAX_GENERAL_TEXTURES 1024
#define MAX_HEIGHTMAP_TEXTURES 256
#define MAX_SKY_TEXTURES 16
namespace DescriptorIndices
{
    constexpr UINT PER_FRAME_CBV_START = 0;
    constexpr UINT PER_SCENE_CBV = g_FrameCount;    // index after all per-frame CBVs
    constexpr UINT TEXTURE_SRV = PER_SCENE_CBV + 1; // SRV for texture: after all CBVs
    constexpr UINT HEIGHTMAP_SRV = TEXTURE_SRV + 1; // SRV for heightmap: after all CBVs
    constexpr UINT SKY_SRV = HEIGHTMAP_SRV + MAX_HEIGHTMAP_TEXTURES;
    constexpr UINT NUM_DESCRIPTORS = SKY_SRV + MAX_SKY_TEXTURES;
}
static UINT g_errorHeightmapIndex = 0;

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
    ID3D12GraphicsCommandList *m_commandList[g_FrameCount];
    ID3D12RootSignature *m_rootSignature;

    // depth buffer
    ID3D12DescriptorHeap *m_dsvHeap;
    ID3D12Resource *m_depthStencil;

    ID3D12PipelineState *m_pipelineStates[RenderPipeline::RENDER_COUNT][4]; // 1x, 2x, 4x, 8x

    // MSAA resources
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
        HRAssert(
            pipeline_dx12.m_commandList[sync_state.m_frameIndex]->Reset(
                pipeline_dx12.m_commandAllocators[sync_state.m_frameIndex],
                pipeline_dx12.m_pipelineStates[RenderPipeline::RENDER_DEFAULT][psoIndex]));
    }
} pipeline_dx12;

struct PerDrawRootConstants
{
    DirectX::XMFLOAT4X4 world; // NOTE: changing this to XMFLOAT3x4 screws up rendering on faster gpu for some reason??????
    static_assert(sizeof(world) == 16 * 4, "Dont change type of world matrix, it screws up everything on test bench for some reason (faster GPU)");

    UINT heightmapIndex;
    UINT padding[3]; // ensure 16‑byte alignment
};
static_assert((sizeof(PerDrawRootConstants) <= 256), "Root32BitConstants size must be 256-bytes or smaller (64 DWORDS)");

static struct
{
    PerFrameConstantBuffer m_PerFrameConstantBufferData[g_FrameCount];
    PerSceneConstantBuffer m_PerSceneConstantBufferData;

    D3D12_VERTEX_BUFFER_VIEW m_vertexBufferView[PrimitiveType::PRIMITIVE_COUNT] = {};
    D3D12_INDEX_BUFFER_VIEW m_indexBufferView[PrimitiveType::PRIMITIVE_COUNT] = {};
    ID3D12Resource *m_vertexBuffer[PrimitiveType::PRIMITIVE_COUNT] = {};
    ID3D12Resource *m_indexBuffer[PrimitiveType::PRIMITIVE_COUNT] = {};
    UINT m_indexCount[PrimitiveType::PRIMITIVE_COUNT] = {};

    ID3D12Resource *m_texture = nullptr;
    ID3D12Resource *m_PerSceneConstantBuffer = nullptr;
    UINT8 *m_pPerSceneCbvDataBegin = nullptr;
    ID3D12Resource *m_PerFrameConstantBuffer[g_FrameCount] = {};
    UINT8 *m_pCbvDataBegin[g_FrameCount] = {};

    ID3D12Resource *m_heightmapTexture = nullptr;
    UINT8 *m_heightmapData = nullptr; // CPU copy for editing
    UINT m_heightmapWidth = 256;
    UINT m_heightmapHeight = 256;

    ID3D12Resource *m_heightfieldVertexBuffer = nullptr;
    D3D12_VERTEX_BUFFER_VIEW m_heightfieldVertexView;
    ID3D12Resource *m_heightfieldIndexBuffer = nullptr;
    D3D12_INDEX_BUFFER_VIEW m_heightfieldIndexView;
    UINT m_heightfieldIndexCount;
    ID3D12Resource *m_heightfieldVertexUpload = nullptr;
    ID3D12Resource *m_heightfieldIndexUpload = nullptr;

    ID3D12Resource *m_heightmapResources[MAX_HEIGHTMAP_TEXTURES] = {}; // textures by heap slot
    UINT m_heightmapIndices[MAX_SCENE_OBJECTS] = {};                   // per‑object heap index
    UINT m_nextHeightmapIndex = 0;                                     // next free slot (starts at 0)

    ID3D12Resource *m_skyResources[MAX_SKY_TEXTURES] = {};
    UINT m_skyIndices[MAX_SCENE_OBJECTS] = {}; // per‑object texture index
    UINT m_nextSkyIndex = 0;

    // todo: merge m_heightmapIndices and m_skyIndices

    std::vector<ID3D12Resource *> m_textureUploadHeaps;
} graphics_resources;

bool LoadTextureFromFile(ID3D12Device *device,
                         ID3D12GraphicsCommandList *cmdList,
                         const char *path,
                         ID3D12Resource **outResource,
                         UINT *outIndex) // returns allocated index
{
    // Convert narrow string to wide string
    int wlen = MultiByteToWideChar(CP_UTF8, 0, path, -1, nullptr, 0);
    std::wstring wpath(wlen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, path, -1, &wpath[0], wlen);

    DirectX::ScratchImage image;
    HRESULT hr = DirectX::LoadFromDDSFile(wpath.c_str(), DirectX::DDS_FLAGS_NONE, nullptr, image);
    if (FAILED(hr))
    {
        SDL_Log("Failed to load DDS: %s", path);
        return false;
    }

    const DirectX::TexMetadata &metadata = image.GetMetadata();
    const DirectX::Image *img = image.GetImage(0, 0, 0);
    if (!img)
        return false;

    D3D12_RESOURCE_DESC texDesc = {};
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width = metadata.width;
    texDesc.Height = metadata.height;
    texDesc.DepthOrArraySize = 1;
    texDesc.MipLevels = (UINT16)metadata.mipLevels;
    texDesc.Format = metadata.format;
    texDesc.SampleDesc.Count = 1;
    texDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    hr = device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE,
        &texDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(outResource));
    if (FAILED(hr))
        return false;

    UINT64 uploadSize = GetRequiredIntermediateSize(*outResource, 0, (UINT)image.GetImageCount());
    ID3D12Resource *uploadHeap = nullptr;
    hr = device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
        D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Buffer(uploadSize),
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&uploadHeap));
    if (FAILED(hr))
    {
        (*outResource)->Release();
        *outResource = nullptr;
        return false;
    }

    std::vector<D3D12_SUBRESOURCE_DATA> subresources(image.GetImageCount());
    for (size_t i = 0; i < image.GetImageCount(); ++i)
    {
        const DirectX::Image *subImg = image.GetImage(i, 0, 0);
        subresources[i].pData = subImg->pixels;
        subresources[i].RowPitch = subImg->rowPitch;
        subresources[i].SlicePitch = subImg->slicePitch;
    }

    UpdateSubresources(cmdList, *outResource, uploadHeap, 0, 0, (UINT)image.GetImageCount(), subresources.data());

    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        *outResource,
        D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    cmdList->ResourceBarrier(1, &barrier);

    // Allocate index in the heightmap heap
    UINT index = graphics_resources.m_nextHeightmapIndex;
    if (index >= MAX_HEIGHTMAP_TEXTURES)
    {
        SDL_Log("Out of heightmap texture slots!");
        (*outResource)->Release();
        *outResource = nullptr;
        uploadHeap->Release();
        return false;
    }

    // Create SRV in the main heap at DescriptorIndices::HEIGHTMAP_SRV + index
    CD3DX12_CPU_DESCRIPTOR_HANDLE cpuHandle(
        pipeline_dx12.m_mainHeap->GetCPUDescriptorHandleForHeapStart(),
        DescriptorIndices::HEIGHTMAP_SRV + index,
        g_cbvSrvDescriptorSize);
    device->CreateShaderResourceView(*outResource, nullptr, cpuHandle);

    // Store the resource in the array
    graphics_resources.m_heightmapResources[index] = *outResource;
    graphics_resources.m_nextHeightmapIndex = index + 1;

    *outIndex = index;

    // Track upload heap for later release
    graphics_resources.m_textureUploadHeaps.push_back(uploadHeap);

    return true;
}

bool LoadSkyTextureFromFile(ID3D12Device *device,
                            ID3D12GraphicsCommandList *cmdList,
                            const char *path,
                            ID3D12Resource **outResource,
                            UINT *outIndex) // returns allocated index
{
    // Convert narrow string to wide string
    int wlen = MultiByteToWideChar(CP_UTF8, 0, path, -1, nullptr, 0);
    std::wstring wpath(wlen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, path, -1, &wpath[0], wlen);

    DirectX::ScratchImage image;
    HRESULT hr = DirectX::LoadFromDDSFile(wpath.c_str(), DirectX::DDS_FLAGS_NONE, nullptr, image);
    if (FAILED(hr))
    {
        SDL_Log("Failed to load DDS: %s", path);
        return false;
    }

    const DirectX::TexMetadata &metadata = image.GetMetadata();
    const DirectX::Image *img = image.GetImage(0, 0, 0);
    if (!img)
        return false;

    D3D12_RESOURCE_DESC texDesc = {};
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width = metadata.width;
    texDesc.Height = metadata.height;
    texDesc.DepthOrArraySize = 1;
    texDesc.MipLevels = (UINT16)metadata.mipLevels;
    texDesc.Format = metadata.format;
    texDesc.SampleDesc.Count = 1;
    texDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    hr = device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE,
        &texDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(outResource));
    if (FAILED(hr))
        return false;

    UINT64 uploadSize = GetRequiredIntermediateSize(*outResource, 0, (UINT)image.GetImageCount());
    ID3D12Resource *uploadHeap = nullptr;
    hr = device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
        D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Buffer(uploadSize),
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&uploadHeap));
    if (FAILED(hr))
    {
        (*outResource)->Release();
        *outResource = nullptr;
        return false;
    }

    std::vector<D3D12_SUBRESOURCE_DATA> subresources(image.GetImageCount());
    for (size_t i = 0; i < image.GetImageCount(); ++i)
    {
        const DirectX::Image *subImg = image.GetImage(i, 0, 0);
        subresources[i].pData = subImg->pixels;
        subresources[i].RowPitch = subImg->rowPitch;
        subresources[i].SlicePitch = subImg->slicePitch;
    }

    UpdateSubresources(cmdList, *outResource, uploadHeap, 0, 0, (UINT)image.GetImageCount(), subresources.data());

    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        *outResource,
        D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    cmdList->ResourceBarrier(1, &barrier);

    // Allocate index in the sky texture array
    UINT index = graphics_resources.m_nextSkyIndex;
    if (index >= MAX_SKY_TEXTURES)
    {
        SDL_Log("Out of sky texture slots!");
        (*outResource)->Release();
        *outResource = nullptr;
        uploadHeap->Release();
        return false;
    }

    // Create SRV in the main heap at DescriptorIndices::SKY_SRV + index
    CD3DX12_CPU_DESCRIPTOR_HANDLE cpuHandle(
        pipeline_dx12.m_mainHeap->GetCPUDescriptorHandleForHeapStart(),
        DescriptorIndices::SKY_SRV + index,
        g_cbvSrvDescriptorSize);
    device->CreateShaderResourceView(*outResource, nullptr, cpuHandle);

    // Store the resource in the array
    graphics_resources.m_skyResources[index] = *outResource;
    graphics_resources.m_nextSkyIndex = index + 1;

    *outIndex = index;

    // Track upload heap for later release (reuse same vector)
    graphics_resources.m_textureUploadHeaps.push_back(uploadHeap);

    return true;
}

bool CreateHeightfieldMesh(
    ID3D12Device *device,
    ID3D12GraphicsCommandList *cmdList,
    UINT gridSize, // number of quads per side (power of two)
    ID3D12Resource *&outVertexBuffer,
    D3D12_VERTEX_BUFFER_VIEW &outVertexView,
    ID3D12Resource *&outIndexBuffer,
    D3D12_INDEX_BUFFER_VIEW &outIndexView,
    UINT &outIndexCount)
{
    // Generate vertices (position, normal, uv)
    const UINT verticesPerSide = gridSize + 1;
    const UINT vertexCount = verticesPerSide * verticesPerSide;
    const UINT indexCount = gridSize * gridSize * 6; // two triangles per quad

    std::vector<Vertex> vertices(vertexCount);
    std::vector<uint32_t> indices(indexCount);

    float step = 1.0f / gridSize; // world units? We'll keep positions in [-0.5,0.5] range for simplicity.
    // Generate vertices in row‑major order: z first then x (so that z is row, x is column)
    for (UINT j = 0; j < verticesPerSide; ++j)
    {
        float z = -0.5f + j * step;
        for (UINT i = 0; i < verticesPerSide; ++i)
        {
            float x = -0.5f + i * step;
            UINT idx = j * verticesPerSide + i;
            vertices[idx].position = {x, 0.0f, z};
            vertices[idx].normal = {0.0f, 1.0f, 0.0f}; // flat
            vertices[idx].uv = {(float)i / gridSize, (float)j / gridSize};
        }
    }

    // Generate indices (two triangles per quad)
    UINT idx = 0;
    for (UINT j = 0; j < gridSize; ++j)
    {
        for (UINT i = 0; i < gridSize; ++i)
        {
            UINT bl = j * verticesPerSide + i;
            UINT br = j * verticesPerSide + i + 1;
            UINT tl = (j + 1) * verticesPerSide + i;
            UINT tr = (j + 1) * verticesPerSide + i + 1;
            // triangle 1: bl - tl - br
            indices[idx++] = bl;
            indices[idx++] = tl;
            indices[idx++] = br;
            // triangle 2: br - tl - tr
            indices[idx++] = br;
            indices[idx++] = tl;
            indices[idx++] = tr;
        }
    }

    // Create vertex buffer using CreateDefaultBuffer (upload then default)
    ID3D12Resource *uploadVertex = nullptr;
    if (!CreateDefaultBuffer(device, cmdList, vertices.data(), vertexCount * sizeof(Vertex),
                             D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER,
                             &outVertexBuffer, &uploadVertex))
    {
        return false;
    }
    // We need to keep uploadVertex until after command list execution. We'll store it similarly to primitive upload buffers.
    // For simplicity, we can create a static array or store in graphics_resources and release after WaitForGpu.
    // We'll add a member to graphics_resources to hold the upload buffer.
    graphics_resources.m_heightfieldVertexUpload = uploadVertex;

    // Create index buffer similarly
    ID3D12Resource *uploadIndex = nullptr;
    if (!CreateDefaultBuffer(device, cmdList, indices.data(), indexCount * sizeof(uint32_t),
                             D3D12_RESOURCE_STATE_INDEX_BUFFER,
                             &outIndexBuffer, &uploadIndex))
    {
        return false;
    }
    graphics_resources.m_heightfieldIndexUpload = uploadIndex;

    // Set views
    outVertexView.BufferLocation = outVertexBuffer->GetGPUVirtualAddress();
    outVertexView.StrideInBytes = sizeof(Vertex);
    outVertexView.SizeInBytes = vertexCount * sizeof(Vertex);

    outIndexView.BufferLocation = outIndexBuffer->GetGPUVirtualAddress();
    outIndexView.SizeInBytes = indexCount * sizeof(uint32_t);
    outIndexView.Format = DXGI_FORMAT_R32_UINT;

    outIndexCount = indexCount;
    return true;
}

void WaitForFrameReady()
{
    // Wait if the next frame we want to use isn't ready yet
    if (sync_state.m_fence->GetCompletedValue() < sync_state.m_fenceValues[sync_state.m_frameIndex])
    {
        HRAssert(sync_state.m_fence->SetEventOnCompletion(sync_state.m_fenceValues[sync_state.m_frameIndex], sync_state.m_fenceEvent));
        WaitForSingleObjectEx(sync_state.m_fenceEvent, INFINITE, FALSE);
    }
}

void SignalFrameComplete()
{
    const UINT64 currentFenceValue = sync_state.m_fenceValues[sync_state.m_frameIndex];
    HRAssert(pipeline_dx12.m_commandQueue->Signal(sync_state.m_fence, currentFenceValue));
    sync_state.m_fenceValues[sync_state.m_frameIndex] = currentFenceValue + 1;
}

void AdvanceFrameIndex()
{
    sync_state.m_frameIndex = pipeline_dx12.m_swapChain->GetCurrentBackBufferIndex();
}

// Wait for pending GPU work to complete.
void WaitForGpu()
{
    // Schedule a Signal command in the queue.
    HRAssert(pipeline_dx12.m_commandQueue->Signal(sync_state.m_fence, sync_state.m_fenceValues[sync_state.m_frameIndex]));
    // Wait until the fence has been processed.
    HRAssert(sync_state.m_fence->SetEventOnCompletion(sync_state.m_fenceValues[sync_state.m_frameIndex], sync_state.m_fenceEvent));
    WaitForSingleObjectEx(sync_state.m_fenceEvent, INFINITE, FALSE);

    // Increment the fence value for the current frame.
    // sync_state.m_fenceValues[sync_state.m_frameIndex]++;
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

void RecreateSwapChain()
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
    bool useDxDebugLayer = true;
    if (useDxDebugLayer)
    {
        ID3D12Debug *debugController;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
        {
            debugController->EnableDebugLayer();
            // debugController->SetEnableGPUBasedValidation(true);  // Extra validation
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
        mainHeapDesc.NumDescriptors = DescriptorIndices::NUM_DESCRIPTORS; // Texture + per-frame CBVs + per-scene CBV
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
            pData[n] = 0x55;     // R
            pData[n + 1] = 0x55; // G
            pData[n + 2] = 0x55; // B
            pData[n + 3] = 0xff; // A
        }
        else
        {
            pData[n] = 0x6c;     // R
            pData[n + 1] = 0x6c; // G
            pData[n + 2] = 0x6c; // B
            pData[n + 3] = 0xff; // A
        }
    }

    return data;
}

// Helper to compile a shader from file with common settings.
// Returns true on success, false on failure. On success, outBlob contains the compiled shader.
bool CompileShader(
    const wchar_t *filename,
    const char *entryPoint,
    const char *target,
    ID3DBlob **outBlob,
    const D3D_SHADER_MACRO *defines = nullptr)
{
    UINT compileFlags = 0;
#if defined(_DEBUG)
    compileFlags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
    compileFlags |= D3DCOMPILE_ENABLE_UNBOUNDED_DESCRIPTOR_TABLES;

    ID3DBlob *errorBlob = nullptr;
    HRESULT hr = D3DCompileFromFile(
        filename,
        defines,
        nullptr, // no include handler
        entryPoint,
        target,
        compileFlags,
        0, // no effect flags
        outBlob,
        &errorBlob);

    if (FAILED(hr))
    {
        if (errorBlob)
        {
            SDL_Log((const char *)errorBlob->GetBufferPointer());
            errorBlob->Release();
        }
        else
        {
            SDL_Log("Failed to compile shader: %S, entry %s, target %s", filename, entryPoint, target);
        }
        return false;
    } else {
        SDL_Log("Compiled %s (%s) - blob size %zu", entryPoint, target, (*outBlob)->GetBufferSize());     
    }    
    return true;
}

#include "generated/pipeline_creation.cpp"
// Load the startup assets. Returns true on success, false on fail.
bool LoadAssets()
{
    // Create root signature (using 1.0 structures for compatibility)
    {
        CD3DX12_DESCRIPTOR_RANGE cbvRange;
        cbvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 2);

        CD3DX12_DESCRIPTOR_RANGE srvRanges[3];
        srvRanges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
        srvRanges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, MAX_HEIGHTMAP_TEXTURES, 1);
        srvRanges[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, MAX_SKY_TEXTURES, 1 + MAX_HEIGHTMAP_TEXTURES); // base register = 257

        CD3DX12_ROOT_PARAMETER rootParameters[4];
        rootParameters[0].InitAsConstants(sizeof(PerDrawRootConstants) / 4, 0, 0, D3D12_SHADER_VISIBILITY_ALL);
        rootParameters[1].InitAsConstantBufferView(1, 0, D3D12_SHADER_VISIBILITY_ALL);
        rootParameters[2].InitAsDescriptorTable(1, &cbvRange, D3D12_SHADER_VISIBILITY_ALL);
        rootParameters[3].InitAsDescriptorTable(3, srvRanges, D3D12_SHADER_VISIBILITY_ALL);

        D3D12_STATIC_SAMPLER_DESC sampler = {};
        sampler.Filter = D3D12_FILTER_ANISOTROPIC;
        sampler.MaxAnisotropy = 16;
        sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        sampler.MipLODBias = 0;
        sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
        sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
        sampler.MinLOD = 0.0f;
        sampler.MaxLOD = D3D12_FLOAT32_MAX;
        sampler.ShaderRegister = 0;
        sampler.RegisterSpace = 0;
        sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDesc;
        rootSignatureDesc.Init(_countof(rootParameters), rootParameters, 1, &sampler, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

        ID3DBlob *signature;
        ID3DBlob *error;
        HRESULT hr = D3D12SerializeRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error);
        if (FAILED(hr))
        {
            if (error)
            {
                SDL_Log("Root signature serialization error: %s", (const char *)error->GetBufferPointer());
                error->Release();
            }
            HRAssert(hr);
            return false;
        }
        if (!HRAssert(pipeline_dx12.m_device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&pipeline_dx12.m_rootSignature))))
            return false;
    }
    // Create the pipeline states, which includes compiling and loading shaders.
    // Define the vertex input layout.
    D3D12_INPUT_ELEMENT_DESC inputElementDescs[] =
        {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}};

    if (!CreateAllPipelines(inputElementDescs, _countof(inputElementDescs)))
        return false;

    // Create the command lists
    for (UINT i = 0; i < g_FrameCount; ++i)
    {
        HRAssert(
            pipeline_dx12.m_device->CreateCommandList(
                0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                pipeline_dx12.m_commandAllocators[i],
                pipeline_dx12.m_pipelineStates[RenderPipeline::RENDER_DEFAULT][0],
                IID_PPV_ARGS(&pipeline_dx12.m_commandList[i])));
        HRAssert(pipeline_dx12.m_commandList[i]->Close());
    }

    // Reset the first command list for setup recording
    HRAssert(pipeline_dx12.m_commandList[0]->Reset(
        pipeline_dx12.m_commandAllocators[0],
        pipeline_dx12.m_pipelineStates[RenderPipeline::RENDER_DEFAULT][0]));

    for (UINT i = 0; i < PrimitiveType::PRIMITIVE_COUNT; ++i)
    {
        PrimitiveType type = static_cast<PrimitiveType>(i);
        if (!CreatePrimitiveMeshBuffers(
                pipeline_dx12.m_device,
                pipeline_dx12.m_commandList[0],
                type,
                kPrimitiveMeshData[i],
                graphics_resources.m_vertexBuffer[i],
                graphics_resources.m_vertexBufferView[i],
                graphics_resources.m_indexBuffer[i],
                graphics_resources.m_indexBufferView[i],
                graphics_resources.m_indexCount[i]))
        {
            return false;
        }
    }

    CD3DX12_CPU_DESCRIPTOR_HANDLE cpuStart(pipeline_dx12.m_mainHeap->GetCPUDescriptorHandleForHeapStart());
    UINT cbvSrvDescriptorSize = pipeline_dx12.m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    g_cbvSrvDescriptorSize = cbvSrvDescriptorSize;

    // Create fallback error texture (4x4 pink/black checkerboard)
    {
        const UINT errWidth = 4, errHeight = 4;
        std::vector<uint32_t> errData(errWidth * errHeight);
        for (UINT y = 0; y < errHeight; ++y)
        {
            for (UINT x = 0; x < errWidth; ++x)
            {
                bool isMagenta = ((x ^ y) & 1) != 0;
                errData[y * errWidth + x] = isMagenta ? 0xFFFF00FF : 0xFF000000;
            }
        }

        D3D12_HEAP_PROPERTIES heapProps = {};
        heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
        heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        heapProps.CreationNodeMask = 1;
        heapProps.VisibleNodeMask = 1;

        D3D12_RESOURCE_DESC errDesc = {};
        errDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        errDesc.Width = errWidth;
        errDesc.Height = errHeight;
        errDesc.DepthOrArraySize = 1;
        errDesc.MipLevels = 1;
        errDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        errDesc.SampleDesc.Count = 1;
        errDesc.SampleDesc.Quality = 0;
        errDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        errDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

        // Check device removed reason first
        HRESULT removal = pipeline_dx12.m_device->GetDeviceRemovedReason();
        if (removal != S_OK)
        {
            SDL_Log("Device removed before texture creation: 0x%08X", removal);
        }

        HRESULT hr = pipeline_dx12.m_device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &errDesc,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(&graphics_resources.m_heightmapResources[0]));

        if (FAILED(hr))
        {
            SDL_Log("CreateCommittedResource failed with HRESULT 0x%08X", hr);
            HRAssert(hr);
            return false; // or handle appropriately
        }

        ID3D12Resource *errUpload = nullptr;
        UINT64 uploadSize = GetRequiredIntermediateSize(graphics_resources.m_heightmapResources[0], 0, 1);
        HRAssert(pipeline_dx12.m_device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer(uploadSize),
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&errUpload)));

        D3D12_SUBRESOURCE_DATA subData = {};
        subData.pData = errData.data();
        subData.RowPitch = errWidth * 4;
        subData.SlicePitch = subData.RowPitch * errHeight;

        UpdateSubresources(pipeline_dx12.m_commandList[0],
                           graphics_resources.m_heightmapResources[0],
                           errUpload,
                           0, 0, 1, &subData);

        auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
            graphics_resources.m_heightmapResources[0],
            D3D12_RESOURCE_STATE_COPY_DEST,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        pipeline_dx12.m_commandList[0]->ResourceBarrier(1, &barrier);

        graphics_resources.m_textureUploadHeaps.push_back(errUpload);

        // After resource creation, create SRV at HEIGHTMAP_SRV + 0
        UINT errorIndex = 0;
        CD3DX12_CPU_DESCRIPTOR_HANDLE cpuHandle(
            pipeline_dx12.m_mainHeap->GetCPUDescriptorHandleForHeapStart(),
            DescriptorIndices::HEIGHTMAP_SRV + errorIndex,
            cbvSrvDescriptorSize);
        pipeline_dx12.m_device->CreateShaderResourceView(graphics_resources.m_heightmapResources[0], nullptr, cpuHandle);

        graphics_resources.m_heightmapResources[errorIndex] = graphics_resources.m_heightmapResources[0];
        graphics_resources.m_nextHeightmapIndex = 1; // next free index is 1
        g_errorHeightmapIndex = errorIndex;
    }

    // create the shared heightfield mesh
    const UINT heightfieldGridSize = 256; // 256x256 quads -> 257x257 vertices
    if (!CreateHeightfieldMesh(
            pipeline_dx12.m_device,
            pipeline_dx12.m_commandList[0],
            heightfieldGridSize,
            graphics_resources.m_heightfieldVertexBuffer,
            graphics_resources.m_heightfieldVertexView,
            graphics_resources.m_heightfieldIndexBuffer,
            graphics_resources.m_heightfieldIndexView,
            graphics_resources.m_heightfieldIndexCount))
    {
        HRAssert(E_ABORT);
        return false;
    }

    // create per frame constant buffer for each frame in the frame buffer
    for (UINT i = 0; i < g_FrameCount; i++)
    {
        const UINT PerFrameConstantBufferSize = sizeof(PerFrameConstantBuffer); // CB size is required to be 256-byte aligned.

        if (!HRAssert(pipeline_dx12.m_device->CreateCommittedResource(
                &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
                D3D12_HEAP_FLAG_NONE,
                &CD3DX12_RESOURCE_DESC::Buffer(PerFrameConstantBufferSize),
                D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr,
                IID_PPV_ARGS(&graphics_resources.m_PerFrameConstantBuffer[i]))))
            return false;

        // Describe and create a constant buffer view.
        D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
        cbvDesc.BufferLocation = graphics_resources.m_PerFrameConstantBuffer[i]->GetGPUVirtualAddress();
        cbvDesc.SizeInBytes = PerFrameConstantBufferSize;

        CD3DX12_CPU_DESCRIPTOR_HANDLE cbvHandle(
            cpuStart,
            (INT)i,
            cbvSrvDescriptorSize);
        pipeline_dx12.m_device->CreateConstantBufferView(&cbvDesc, cbvHandle);

        // Map and initialize the constant buffer. We don't unmap this until the
        // app closes. Keeping things mapped for the lifetime of the resource is okay.
        CD3DX12_RANGE readRange(0, 0); // We do not intend to read from this resource on the CPU.
        if (!HRAssert(graphics_resources.m_PerFrameConstantBuffer[i]->Map(0, &readRange, reinterpret_cast<void **>(&graphics_resources.m_pCbvDataBegin[i]))))
            return false;
        memcpy(graphics_resources.m_pCbvDataBegin[i], &graphics_resources.m_PerFrameConstantBufferData, sizeof(graphics_resources.m_PerFrameConstantBufferData));
    }

    // create per scene constant buffer that just sits and gets updated rarely
    {
        const UINT PerSceneConstantBufferSize = sizeof(PerSceneConstantBuffer); // CB size is required to be 256-byte aligned.

        HRAssert(pipeline_dx12.m_device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer(PerSceneConstantBufferSize),
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&graphics_resources.m_PerSceneConstantBuffer)));

        D3D12_CONSTANT_BUFFER_VIEW_DESC perSceneCbvDesc = {};
        perSceneCbvDesc.BufferLocation = graphics_resources.m_PerSceneConstantBuffer->GetGPUVirtualAddress();
        perSceneCbvDesc.SizeInBytes = PerSceneConstantBufferSize;

        // Per-scene CBV goes at descriptor index g_FrameCount + 1 (after per-frame CBVs)
        CD3DX12_CPU_DESCRIPTOR_HANDLE perSceneCbvHandle(
            cpuStart,
            (INT)(DescriptorIndices::PER_SCENE_CBV),
            cbvSrvDescriptorSize);
        pipeline_dx12.m_device->CreateConstantBufferView(&perSceneCbvDesc, perSceneCbvHandle);

        // todo: abstract this so i can call it from main when i want it updated
        // Map and initialize the per-scene constant buffer
        CD3DX12_RANGE readRange(0, 0);
        HRAssert(graphics_resources.m_PerSceneConstantBuffer->Map(
            0, &readRange,
            reinterpret_cast<void **>(&graphics_resources.m_pPerSceneCbvDataBegin)));

        graphics_resources.m_PerSceneConstantBufferData.ambient_colour = DirectX::XMFLOAT4(0.2f, 0.2f, 0.3f, 1.0f);
        graphics_resources.m_PerSceneConstantBufferData.light_direction = DirectX::XMFLOAT4(0.2f, 0.2f, 0.3f, 1.0f);
        graphics_resources.m_PerSceneConstantBufferData.light_colour = DirectX::XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);

        memcpy(graphics_resources.m_pPerSceneCbvDataBegin,
               &graphics_resources.m_PerSceneConstantBufferData,
               sizeof(graphics_resources.m_PerSceneConstantBufferData));
    }

    // Note: ComPtr's are CPU objects but this resource needs to stay in scope until
    // the command list that references it has finished executing on the GPU.
    // We will flush the GPU at the end of this method to ensure the resource is not
    // prematurely destroyed.
    ID3D12Resource *textureUploadHeap;
    ID3D12Resource *stagingHeap = nullptr;

    // Create the texture.
    {
// Save it as DDS (one-time operation)
#if defined(_DEBUG)
        {
            std::vector<UINT8> texture = GenerateTextureData();
            // Create ScratchImage from your generated data
            DirectX::ScratchImage image;

            // Correct API: Initialize without data first
            HRESULT hr = image.Initialize2D(
                DXGI_FORMAT_R8G8B8A8_UNORM,
                TextureWidth,
                TextureHeight,
                1, // arraySize
                1  // mipLevels (just base level for now)
            );

            if (SUCCEEDED(hr))
            {
                // Get the image and copy data
                const DirectX::Image *firstImage = image.GetImage(0, 0, 0);
                if (firstImage)
                {
                    // Check if rowPitch matches expected
                    if (firstImage->rowPitch == TextureWidth * TexturePixelSize)
                    {
                        memcpy(firstImage->pixels, texture.data(), texture.size());
                    }
                    else
                    {
                        // Copy row by row if pitch differs
                        const UINT8 *src = texture.data();
                        UINT8 *dest = firstImage->pixels;
                        for (UINT row = 0; row < TextureHeight; ++row)
                        {
                            memcpy(dest, src, TextureWidth * TexturePixelSize);
                            dest += firstImage->rowPitch;
                            src += TextureWidth * TexturePixelSize;
                        }
                    }
                }

                // Generate mipmaps
                DirectX::ScratchImage mipChain;
                if (SUCCEEDED(GenerateMipMaps(
                        *image.GetImage(0, 0, 0),
                        DirectX::TEX_FILTER_BOX,
                        0, // Generate all mip levels
                        mipChain)))
                {
                    // Save to DDS file
                    hr = DirectX::SaveToDDSFile(
                        mipChain.GetImages(),
                        mipChain.GetImageCount(),
                        mipChain.GetMetadata(),
                        DirectX::DDS_FLAGS_NONE,
                        L"assets/checkerboard.dds");

                    if (SUCCEEDED(hr))
                    {
                        SDL_Log("Saved texture to checkerboard.dds with %zu mip levels",
                                mipChain.GetImageCount());
                    }
                }
            }
        }
#endif

        // Now load from DDS file
        DirectX::ScratchImage loadedImage;
        HRESULT hr = DirectX::LoadFromDDSFile(
            L"assets/checkerboard.dds",
            DirectX::DDS_FLAGS_NONE,
            nullptr,
            loadedImage);

        if (!HRAssert(hr, "Failed to load checkerboard.dds"))
            return false;

        const DirectX::TexMetadata &metadata = loadedImage.GetMetadata();
        const DirectX::Image *images = loadedImage.GetImages();
        size_t imageCount = loadedImage.GetImageCount();

        SDL_Log("Loaded DDS with %zu mip levels, %zu x %zu",
                metadata.mipLevels, metadata.width, metadata.height);

        // Cast to avoid conversion warnings
        UINT mipLevelsUINT = static_cast<UINT>(metadata.mipLevels);
        UINT widthUINT = static_cast<UINT>(metadata.width);
        UINT heightUINT = static_cast<UINT>(metadata.height);
        UINT imageCountUINT = static_cast<UINT>(imageCount);

        // Describe and create a Texture2D WITH MIP LEVELS
        D3D12_RESOURCE_DESC textureDesc = {};
        textureDesc.MipLevels = static_cast<UINT16>(mipLevelsUINT); // Use loaded mip levels!
        textureDesc.Format = metadata.format;
        textureDesc.Width = widthUINT;
        textureDesc.Height = heightUINT;
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

        // Calculate upload buffer size for ALL mip levels
        const UINT64 uploadBufferSize = GetRequiredIntermediateSize(
            graphics_resources.m_texture,
            0,
            imageCountUINT // Already cast to UINT
        );

        // Create the GPU upload buffer.
        if (!HRAssert(pipeline_dx12.m_device->CreateCommittedResource(
                &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
                D3D12_HEAP_FLAG_NONE,
                &CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize),
                D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr,
                IID_PPV_ARGS(&textureUploadHeap))))
            return false;

        // Prepare subresource data for ALL mip levels
        std::vector<D3D12_SUBRESOURCE_DATA> subresources(imageCount);
        for (size_t i = 0; i < imageCount; ++i)
        {
            subresources[i].pData = images[i].pixels;
            subresources[i].RowPitch = static_cast<LONG_PTR>(images[i].rowPitch);
            subresources[i].SlicePitch = static_cast<LONG_PTR>(images[i].slicePitch);
        }

        // Copy ALL mip levels to the GPU
        UpdateSubresources(pipeline_dx12.m_commandList[0],
                           graphics_resources.m_texture,
                           textureUploadHeap,
                           0, 0,
                           imageCountUINT, // Already cast to UINT
                           subresources.data());

        auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
            graphics_resources.m_texture,
            D3D12_RESOURCE_STATE_COPY_DEST,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        pipeline_dx12.m_commandList[0]->ResourceBarrier(1, &barrier);

        UINT increment = pipeline_dx12.m_device->GetDescriptorHandleIncrementSize(
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        CD3DX12_CPU_DESCRIPTOR_HANDLE cpuSrv(cpuStart);
        cpuSrv.Offset(DescriptorIndices::TEXTURE_SRV, increment);

        // Describe and create a SRV for the texture WITH ALL MIP LEVELS
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Format = textureDesc.Format;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MostDetailedMip = 0;
        srvDesc.Texture2D.MipLevels = mipLevelsUINT; // ALL mips!
        srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
        pipeline_dx12.m_device->CreateShaderResourceView(graphics_resources.m_texture, &srvDesc, cpuSrv);
    }

    {
        // Heightmap texture (R8_UNORM, 256x256)
        const UINT hmWidth = 256, hmHeight = 256;
        const UINT hmPixelSize = 1;                         // bytes per pixel
        std::vector<UINT8> hmData(hmWidth * hmHeight, 128); // mid grey

        // todo: load from dds here, if not exist then make up one then write it, and reload it
        for (int x = 0; x < hmWidth; ++x)
        {
            for (int y = 0; y < hmWidth; ++y)
            {
                hmData[x + y * hmWidth] = (int)(rand() % 255);
            }
        }

        // Describe the texture
        D3D12_RESOURCE_DESC hmDesc = {};
        hmDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        hmDesc.Width = hmWidth;
        hmDesc.Height = hmHeight;
        hmDesc.DepthOrArraySize = 1;
        hmDesc.MipLevels = 1;
        hmDesc.Format = DXGI_FORMAT_R8_UNORM;
        hmDesc.SampleDesc.Count = 1;
        hmDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

        // Create default heap texture
        HRAssert(pipeline_dx12.m_device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
            D3D12_HEAP_FLAG_NONE,
            &hmDesc,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(&graphics_resources.m_heightmapTexture)));

        // Upload via staging heap
        UINT64 uploadSize = GetRequiredIntermediateSize(graphics_resources.m_heightmapTexture, 0, 1);
        HRAssert(pipeline_dx12.m_device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer(uploadSize),
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&stagingHeap)));

        D3D12_SUBRESOURCE_DATA subData = {};
        subData.pData = hmData.data();
        subData.RowPitch = hmWidth * hmPixelSize;
        subData.SlicePitch = subData.RowPitch * hmHeight;

        UpdateSubresources(pipeline_dx12.m_commandList[0],
                           graphics_resources.m_heightmapTexture,
                           stagingHeap,
                           0, 0, 1, &subData);

        // Transition to shader‑readable state
        auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
            graphics_resources.m_heightmapTexture,
            D3D12_RESOURCE_STATE_COPY_DEST,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        pipeline_dx12.m_commandList[0]->ResourceBarrier(1, &barrier);

        // Create SRV in the main heap at DescriptorIndices::HEIGHTMAP_SRV
        UINT inc = pipeline_dx12.m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        CD3DX12_CPU_DESCRIPTOR_HANDLE cpuSrv(cpuStart);
        cpuSrv.Offset(DescriptorIndices::HEIGHTMAP_SRV, inc);

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Format = DXGI_FORMAT_R8_UNORM;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MostDetailedMip = 0;
        srvDesc.Texture2D.MipLevels = 1;
        pipeline_dx12.m_device->CreateShaderResourceView(graphics_resources.m_heightmapTexture, &srvDesc, cpuSrv);

        // Store CPU data for later editing
        graphics_resources.m_heightmapData = (UINT8 *)malloc(hmWidth * hmHeight);
        memcpy(graphics_resources.m_heightmapData, hmData.data(), hmWidth * hmHeight);
        graphics_resources.m_heightmapWidth = hmWidth;
        graphics_resources.m_heightmapHeight = hmHeight;

        // Add stagingHeap to a list of temporary resources to release after WaitForGpu()
        // (For now, we'll just keep it and release at the end of LoadAssets)
    }

    // Close the command list and execute it to begin the initial GPU setup.
    if (!HRAssert(pipeline_dx12.m_commandList[0]->Close()))
        return false;
    ID3D12CommandList *ppCommandLists[] = {pipeline_dx12.m_commandList[0]};
    pipeline_dx12.m_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

    // Create synchronization objects and wait until assets have been uploaded to the GPU.
    {
        if (!HRAssert(pipeline_dx12.m_device->CreateFence(sync_state.m_fenceValues[sync_state.m_frameIndex], D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&sync_state.m_fence))))
            return false;
        sync_state.m_fenceValues[sync_state.m_frameIndex]++;

        for (UINT i = 0; i < g_FrameCount; i++)
        {
            sync_state.m_fenceValues[i] = 1;
        }

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
    for (int i = 0; i < PrimitiveType::PRIMITIVE_COUNT; ++i)
    {
        g_vertexBufferUploadPrimitives[i]->Release();
        g_indexBufferUploadPrimitives[i]->Release();
    }
    textureUploadHeap->Release();
    stagingHeap->Release();
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