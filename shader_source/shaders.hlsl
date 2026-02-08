// Root constants - register(b0) - per-draw world matrix
cbuffer PerDrawRootConstants : register(b0)
{
    float3x4 partial_world;
};

cbuffer PerFrameConstantBuffer : register(b1)
{
    // float4x4 world;
    float4x4 view;
    float4x4 projection;
    float padding[16+16];
};

struct VSInput
{
    float4 position : POSITION;
    float2 uv : TEXCOORD;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD;
};

Texture2D g_texture : register(t0);
SamplerState g_sampler : register(s0);

PSInput VSMain(VSInput input)
{
    PSInput result;
    float4 position = input.position;

    float4x4 world = float4x4(
        partial_world._11_12_13_14,
        partial_world._21_22_23_24,
        partial_world._31_32_33_34,
        float4(0, 0, 0, 1)
    );

    position = mul(position, world);
    position = mul(position, view);
    position = mul(position, projection);

    result.position = position;
    result.uv = input.uv;

    return result;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    return g_texture.Sample(g_sampler, input.uv);    
}