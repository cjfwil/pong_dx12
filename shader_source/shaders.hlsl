cbuffer ConstantBuffer : register(b0)
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
    // position = mul(position, world);
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