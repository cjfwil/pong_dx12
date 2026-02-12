// common.hlsl – Unified shader for standard UV and triplanar mapping
// 
// Compile with -D TRIPLANAR for triplanar shading, otherwise standard UV mapping.
// All constant buffer definitions and input structures are shared.

// ----------------------------------------------------------------------------
// Constant buffers (identical in both shaders)
// ----------------------------------------------------------------------------
cbuffer PerDrawRootConstants : register(b0)
{
    float4x4 world;
};

cbuffer PerFrameConstantBuffer : register(b1)
{
    float4x4 view;
    float4x4 projection;
    float per_frame_padding[16 + 16];   // 256‑byte alignment
};

cbuffer PerSceneConstantBuffer : register(b2)
{
    float4 ambient_colour;
    float per_scene_padding[60];        // 256‑byte alignment
};

Texture2D g_texture : register(t0);
SamplerState g_sampler : register(s0);

// ----------------------------------------------------------------------------
// Common vertex input and output structures
// ----------------------------------------------------------------------------
struct VSInput
{
    float4 position : POSITION;
    float3 norm    : NORMAL;
    float2 uv      : TEXCOORD;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float3 worldPos : POSITION2;   // world space position (used by triplanar)
    float3 normal   : NORMAL;      // world space (triplanar) or object space (standard)
    float2 uv       : TEXCOORD;
};

// ----------------------------------------------------------------------------
// Triplanar shading path (activated by #define TRIPLANAR)
// ----------------------------------------------------------------------------
#define TRIPLANAR
#ifdef TRIPLANAR

PSInput VSMain(VSInput input)
{
    PSInput result;

    // Transform to world space (row‑major multiplication: vector * matrix)
    float4 worldPosition = mul(input.position, world);
    result.position      = mul(mul(worldPosition, view), projection);
    result.worldPos      = worldPosition.xyz;

    // Transform normal to world space (no translation)
    result.normal        = normalize(mul(input.norm, (float3x3)world));

    result.uv            = input.uv;
    return result;
}

// Triplanar sampling parameters
static const float g_Tiling          = 1.0f;   // texels per world metre
static const float g_BlendSharpness = 1.0f;   // higher = sharper blend between axes

float4 SampleTriplanar(Texture2D tex, SamplerState sam, float3 worldPos, float3 normal, float tiling)
{
    float3 wp = worldPos * tiling;

    // Sample from three planar projections
    float4 colX = tex.Sample(sam, wp.yz); // YZ plane (faces ±X)
    float4 colY = tex.Sample(sam, wp.xz); // XZ plane (faces ±Y)
    float4 colZ = tex.Sample(sam, wp.xy); // XY plane (faces ±Z)

    // Compute blend weights from absolute normal
    float3 blend = abs(normal);
    blend = pow(blend, g_BlendSharpness);
    blend /= (blend.x + blend.y + blend.z); // normalise

    return colX * blend.x + colY * blend.y + colZ * blend.z;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    float4 texColor = SampleTriplanar(g_texture, g_sampler,
                                      input.worldPos, input.normal, g_Tiling);
    return texColor * ambient_colour;
}

// ----------------------------------------------------------------------------
// Standard UV mapping path (default)
// ----------------------------------------------------------------------------
#else

PSInput VSMain(VSInput input)
{
    PSInput result;

    // Transform position through the full MVP chain (row‑major)
    float4 pos = input.position;
    pos = mul(pos, world);
    pos = mul(pos, view);
    pos = mul(pos, projection);

    result.position = pos;
    result.worldPos = 0;            // unused in standard path
    result.normal   = input.norm;   // keep object‑space normal (unused in pixel shader)
    result.uv       = input.uv;
    return result;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    float4 texColor = g_texture.Sample(g_sampler, input.uv);
    return texColor * ambient_colour;
}

#endif