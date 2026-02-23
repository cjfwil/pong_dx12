//------------------------------------------------------------------------
// PIPELINE CREATION – DO NOT EDIT
//   This file was automatically generated.
//   by meta_pipelines.py
//   Generated: 2026-02-23 09:41:26
//------------------------------------------------------------------------


// This file defines CreateAllPipelines(). Include it in LoadAssets()
// after inputElementDescs is defined, and call the function.

#include "renderer_dx12.cpp"
#include "render_pipeline_data.h"

bool CreateAllPipelines(const D3D12_INPUT_ELEMENT_DESC* inputLayout, UINT numInputElements)
{
    ID3DBlob* vertexShaders[RENDER_COUNT] = {};
    ID3DBlob* pixelShaders[RENDER_COUNT] = {};

    if (!CompileShader(L"shader_source\\shaders.hlsl", "VSMain", "vs_5_1", &vertexShaders[RENDER_DEFAULT], nullptr)) {
        HRAssert(E_FAIL);
        return false;
    }

    if (!CompileShader(L"shader_source\\shaders.hlsl", "PSMain", "ps_5_1", &pixelShaders[RENDER_DEFAULT], nullptr)) {
        HRAssert(E_FAIL);
        return false;
    }

    static const D3D_SHADER_MACRO render_triplanar_defines[] = {
        {"TRIPLANAR", "1"},
        {nullptr, nullptr}
    };

    if (!CompileShader(L"shader_source\\shaders.hlsl", "VSMain", "vs_5_1", &vertexShaders[RENDER_TRIPLANAR], render_triplanar_defines)) {
        HRAssert(E_FAIL);
        return false;
    }

    if (!CompileShader(L"shader_source\\shaders.hlsl", "PSMain", "ps_5_1", &pixelShaders[RENDER_TRIPLANAR], render_triplanar_defines)) {
        HRAssert(E_FAIL);
        return false;
    }

    static const D3D_SHADER_MACRO render_heightfield_defines[] = {
        {"HEIGHTFIELD", "1"},
        {nullptr, nullptr}
    };

    if (!CompileShader(L"shader_source\\shaders.hlsl", "VSMain", "vs_5_1", &vertexShaders[RENDER_HEIGHTFIELD], render_heightfield_defines)) {
        HRAssert(E_FAIL);
        return false;
    }

    if (!CompileShader(L"shader_source\\shaders.hlsl", "PSMain", "ps_5_1", &pixelShaders[RENDER_HEIGHTFIELD], render_heightfield_defines)) {
        HRAssert(E_FAIL);
        return false;
    }

    static const D3D_SHADER_MACRO render_sky_defines[] = {
        {"SKY", "1"},
        {nullptr, nullptr}
    };

    if (!CompileShader(L"shader_source\\shaders.hlsl", "VSMain", "vs_5_1", &vertexShaders[RENDER_SKY], render_sky_defines)) {
        HRAssert(E_FAIL);
        return false;
    }

    if (!CompileShader(L"shader_source\\shaders.hlsl", "PSMain", "ps_5_1", &pixelShaders[RENDER_SKY], render_sky_defines)) {
        HRAssert(E_FAIL);
        return false;
    }

    // Create PSO for each supported MSAA level
    for (UINT msaaIdx = 0; msaaIdx < 4; ++msaaIdx)
    {
        if (!msaa_state.m_supported[msaaIdx]) continue;

        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.InputLayout = {inputLayout, numInputElements};
        psoDesc.pRootSignature = pipeline_dx12.m_rootSignature;
        psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        // psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
        psoDesc.DepthStencilState.DepthEnable = true;
        psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
        psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_GREATER;
        psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
        psoDesc.SampleMask = UINT_MAX;
        psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        psoDesc.NumRenderTargets = 1;
        psoDesc.RTVFormats[0] = g_screenFormat;
        psoDesc.SampleDesc.Count = msaa_state.m_sampleCounts[msaaIdx];
        psoDesc.SampleDesc.Quality = 0;

        for (UINT tech = 0; tech < RENDER_COUNT; ++tech)
        {
            if (!vertexShaders[tech] || !pixelShaders[tech]) continue;
            psoDesc.VS = CD3DX12_SHADER_BYTECODE(vertexShaders[tech]);
            psoDesc.PS = CD3DX12_SHADER_BYTECODE(pixelShaders[tech]);

            for (UINT blend = 0; blend < BLEND_COUNT; ++blend)
            {
                // Start from default opaque blend state
                psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
                if (blend == BLEND_ALPHA)
                {
                    psoDesc.BlendState.RenderTarget[0].BlendEnable = TRUE;
                    psoDesc.BlendState.RenderTarget[0].SrcBlend  = D3D12_BLEND_SRC_ALPHA;
                    psoDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
                    psoDesc.BlendState.RenderTarget[0].BlendOp   = D3D12_BLEND_OP_ADD;
                    psoDesc.BlendState.RenderTarget[0].SrcBlendAlpha  = D3D12_BLEND_ONE;
                    psoDesc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
                    psoDesc.BlendState.RenderTarget[0].BlendOpAlpha   = D3D12_BLEND_OP_ADD;
                }
                // For BLEND_OPAQUE, the default opaque state is used (no changes)

                HRAssert(pipeline_dx12.m_device->CreateGraphicsPipelineState(
                    &psoDesc,
                    IID_PPV_ARGS(&pipeline_dx12.m_pipelineStates[tech][blend][msaaIdx])));
            }
        }
    }

    // Release shader blobs
    for (UINT tech = 0; tech < RENDER_COUNT; ++tech)
    {
        if (vertexShaders[tech]) vertexShaders[tech]->Release();
        if (pixelShaders[tech]) pixelShaders[tech]->Release();
    }

    return true;
}