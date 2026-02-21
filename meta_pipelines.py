#!/usr/bin/env python3
"""
meta_pipelines.py - Generate pipeline creation function for all shader variants.
Reads a list of pipeline definitions and produces a .cpp file with a function
CreateAllPipelines() that compiles shaders and creates PSOs.
Call this function from LoadAssets() after defining inputElementDescs.
"""

import sys
from pathlib import Path
import common

PIPELINES = [
    {
        "name": "RENDER_DEFAULT",
        "vs_entry": "VSMain",
        "ps_entry": "PSMain",
        "defines": None,
    },
    {
        "name": "RENDER_TRIPLANAR",
        "vs_entry": "VSMain",
        "ps_entry": "PSMain",
        "defines": [("TRIPLANAR", "1")],
    },
    {
        "name": "RENDER_HEIGHTFIELD",
        "vs_entry": "VSMain",
        "ps_entry": "PSMain",
        "defines": [("HEIGHTFIELD", "1")],
    },
        {
        "name": "RENDER_SKY",
        "vs_entry": "VSMain",
        "ps_entry": "PSMain",
        "defines": [("SKY", "1")],
    },
]

def generate_pipeline_code(output_path: Path) -> bool:
    lines = []
    lines.append(common.make_header("meta_pipelines.py", "PIPELINE CREATION"))
    lines.append("// This file defines CreateAllPipelines(). Include it in LoadAssets()")
    lines.append("// after inputElementDescs is defined, and call the function.\n")

    lines.append("#include \"renderer_dx12.cpp\"")
    lines.append("#include \"render_pipeline_data.h\"\n")

    lines.append("bool CreateAllPipelines(const D3D12_INPUT_ELEMENT_DESC* inputLayout, UINT numInputElements)")
    lines.append("{")

    # Declare shader arrays
    lines.append("    ID3DBlob* vertexShaders[RENDER_COUNT] = {};")
    lines.append("    ID3DBlob* pixelShaders[RENDER_COUNT] = {};\n")

    # Compile each pipeline
    for pl in PIPELINES:
        enum_name = pl["name"]
        vs_entry = pl["vs_entry"]
        ps_entry = pl["ps_entry"]
        defines = pl["defines"]

        def_var = f"{enum_name.lower()}_defines" if defines else "nullptr"

        if defines:
            macro_lines = [f"    static const D3D_SHADER_MACRO {def_var}[] = {{"]
            for name, value in defines:
                macro_lines.append(f'        {{"{name}", "{value}"}},')
            macro_lines.append("        {nullptr, nullptr}\n    };")
            lines.extend(macro_lines)
            lines.append("")

        # Vertex shader
        lines.append(f'    if (!CompileShader(L"shader_source\\\\shaders.hlsl", "{vs_entry}", "vs_5_1", &vertexShaders[{enum_name}], {def_var})) {{')
        lines.append("        HRAssert(E_FAIL);")
        lines.append("        return false;")
        lines.append("    }\n")

        # Pixel shader
        lines.append(f'    if (!CompileShader(L"shader_source\\\\shaders.hlsl", "{ps_entry}", "ps_5_1", &pixelShaders[{enum_name}], {def_var})) {{')
        lines.append("        HRAssert(E_FAIL);")
        lines.append("        return false;")
        lines.append("    }\n")

    # PSO creation loop over MSAA levels
    lines.append("    // Create PSO for each supported MSAA level")
    lines.append("    for (UINT msaaIdx = 0; msaaIdx < 4; ++msaaIdx)")
    lines.append("    {")
    lines.append("        if (!msaa_state.m_supported[msaaIdx]) continue;")
    lines.append("")
    lines.append("        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};")
    lines.append("        psoDesc.InputLayout = {inputLayout, numInputElements};")
    lines.append("        psoDesc.pRootSignature = pipeline_dx12.m_rootSignature;")
    lines.append("        psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);")
    lines.append("        psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);")
    lines.append("        psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);")
    lines.append("        psoDesc.DepthStencilState.DepthEnable = true;")
    lines.append("        psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;")
    lines.append("        psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_GREATER;") ##todo, be able specify reverse or not?
    lines.append("        psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;")
    lines.append("        psoDesc.SampleMask = UINT_MAX;")
    lines.append("        psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;")
    lines.append("        psoDesc.NumRenderTargets = 1;")
    lines.append("        psoDesc.RTVFormats[0] = g_screenFormat;")
    lines.append("        psoDesc.SampleDesc.Count = msaa_state.m_sampleCounts[msaaIdx];")
    lines.append("        psoDesc.SampleDesc.Quality = 0;")
    lines.append("")
    lines.append("        for (UINT tech = 0; tech < RENDER_COUNT; ++tech)")
    lines.append("        {")
    lines.append("            if (vertexShaders[tech] && pixelShaders[tech])")
    lines.append("            {")
    lines.append("                psoDesc.VS = CD3DX12_SHADER_BYTECODE(vertexShaders[tech]);")
    lines.append("                psoDesc.PS = CD3DX12_SHADER_BYTECODE(pixelShaders[tech]);")
    lines.append("                HRAssert(pipeline_dx12.m_device->CreateGraphicsPipelineState(")
    lines.append("                    &psoDesc,")
    lines.append("                    IID_PPV_ARGS(&pipeline_dx12.m_pipelineStates[tech][msaaIdx])));")
    lines.append("            }")
    lines.append("        }")
    lines.append("    }\n")

    # Release shader blobs
    lines.append("    // Release shader blobs")
    lines.append("    for (UINT tech = 0; tech < RENDER_COUNT; ++tech)")
    lines.append("    {")
    lines.append("        if (vertexShaders[tech]) vertexShaders[tech]->Release();")
    lines.append("        if (pixelShaders[tech]) pixelShaders[tech]->Release();")
    lines.append("    }")
    lines.append("")
    lines.append("    return true;")
    lines.append("}")

    return common.write_file_if_changed(output_path, "\n".join(lines))

if __name__ == "__main__":
    output = Path("src/generated/pipeline_creation.cpp")
    success = generate_pipeline_code(output)
    sys.exit(0 if success else 1)