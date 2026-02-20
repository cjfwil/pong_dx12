//------------------------------------------------------------------------
// PIPELINE CREATION – DO NOT EDIT
//   This file was automatically generated.
//   by meta_pipelines.py
//   Generated: 2026-02-20 13:34:48
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

    // Create PSO for each supported MSAA level
    for (UINT msaaIdx = 0; msaaIdx < 4; ++msaaIdx)
    {
        if (!msaa_state.m_supported[msaaIdx]) continue;

        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.InputLayout = {inputLayout, numInputElements};
        psoDesc.pRootSignature = pipeline_dx12.m_rootSignature;
        psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
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
            if (vertexShaders[tech] && pixelShaders[tech])
            {
                psoDesc.VS = CD3DX12_SHADER_BYTECODE(vertexShaders[tech]);
                psoDesc.PS = CD3DX12_SHADER_BYTECODE(pixelShaders[tech]);
                HRAssert(pipeline_dx12.m_device->CreateGraphicsPipelineState(
                    &psoDesc,
                    IID_PPV_ARGS(&pipeline_dx12.m_pipelineStates[tech][msaaIdx])));
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