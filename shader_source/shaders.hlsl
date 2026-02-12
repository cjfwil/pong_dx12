// Root constants - register(b0) - per-draw world matrix
cbuffer PerDrawRootConstants : register(b0)
{
    float4x4 world;
};

// per frame constant buffer, updated every frame
cbuffer PerFrameConstantBuffer : register(b1)
{
    float4x4 view;
    float4x4 projection;
    float per_frame_padding[16+16];
};

// per scene constant buffer, updated in human scale time, like between levels, contains static numbers that change rarely
// NOTE: do not update too frequently (multiple times per frame or once per frame) or cause performance penalty
cbuffer PerSceneConstantBuffer : register(b2)
{
    float4 ambient_colour;
    float per_scene_padding[60];
};

Texture2D g_texture : register(t0);
SamplerState g_sampler : register(s0);

struct VSInput
{
    float4 position : POSITION;
    float3 normal : NORMAL; // ADD THIS – your mesh generator must provide normals
    float2 uv : TEXCOORD;   // optional, not used for triplanar but keep for other uses
};

struct PSInput
{
    float4 position : SV_POSITION;
    float3 worldPos : POSITION2; // world position
    float3 normal : NORMAL;      // world normal
    float2 uv : TEXCOORD;        // optional
};

PSInput VSMain(VSInput input)
{
    PSInput result;

    float4 worldPosition = mul(input.position, world); // row‑major – note order!
    result.position = mul(mul(worldPosition, view), projection);
    result.worldPos = worldPosition.xyz;

    // Transform normal to world space (no translation, only rotation/scale)
    result.normal = normalize(mul(input.normal, (float3x3)world));

    result.uv = input.uv; // pass through if needed
    return result;
}

//todo: expose?
static const float g_Tiling = 1.0f;         // texels per world metre (increase = more repeats)
static const float g_BlendSharpness = 4.0f; // higher = sharper blend between axes

float4 SampleTriplanar(Texture2D tex, SamplerState sam, float3 worldPos, float3 normal, float tiling)
{    
    float3 wp = worldPos * tiling;
 
    float4 colX = tex.Sample(sam, wp.yz); // YZ plane (facing ±X)
    float4 colY = tex.Sample(sam, wp.xz); // XZ plane (facing ±Y)
    float4 colZ = tex.Sample(sam, wp.xy); // XY plane (facing ±Z)

    // Compute blend weights from absolute normal
    float3 blend = abs(normal);
    blend = pow(blend, g_BlendSharpness);
    blend /= (blend.x + blend.y + blend.z); // normalise

    return colX * blend.x + colY * blend.y + colZ * blend.z;
}

float4 PSMain(PSInput input) : SV_TARGET
{    
    float3 worldPos = input.worldPos;
    float3 normal = input.normal;

    float4 texColor = SampleTriplanar(g_texture, g_sampler, worldPos, normal, g_Tiling);
    float4 final = texColor * ambient_colour;
    return final;
}