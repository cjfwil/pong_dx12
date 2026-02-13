// common.hlsl – Unified shader with directional lighting
// Compile with -D TRIPLANAR for triplanar shading, otherwise standard UV mapping.
// Both paths now compute world‑space normals and support a directional light.

// ----------------------------------------------------------------------------
// Constant buffers
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

// Per‑scene constant buffer – now includes directional light data
cbuffer PerSceneConstantBuffer : register(b2)
{    
    float4 ambient_colour;    
    float4 light_direction;
    float4 light_colour;
    // padding to keep 256‑byte alignment
    float per_scene_padding[52];
};

Texture2D g_texture : register(t0);
SamplerState g_sampler : register(s0);

// ----------------------------------------------------------------------------
// Common vertex input / output
// ----------------------------------------------------------------------------
struct VSInput
{
    float4 position : POSITION;
    float3 norm     : NORMAL;
    float2 uv       : TEXCOORD;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float3 worldPos : POSITION2;   // world space position (triplanar) / unused
    float3 normal   : NORMAL;      // world space normal (normalised)
    float2 uv       : TEXCOORD;
};

// ----------------------------------------------------------------------------
// Triplanar path
// ----------------------------------------------------------------------------
#define TRIPLANAR
#ifdef TRIPLANAR

PSInput VSMain(VSInput input)
{
    PSInput result;

    // world position (row‑major: vector * matrix)
    float4 worldPosition = mul(input.position, world);
    result.position      = mul(mul(worldPosition, view), projection);
    result.worldPos      = worldPosition.xyz;

    // world space normal – assume world matrix has no non‑uniform scaling
    result.normal        = normalize(mul(input.norm, (float3x3)world));

    result.uv            = input.uv;
    return result;
}

// Triplanar sampling parameters
static const float g_Tiling          = 1.0f;
static const float g_BlendSharpness = 1.0f;

float4 SampleTriplanar(Texture2D tex, SamplerState sam,
                       float3 worldPos, float3 normal, float tiling)
{
    float3 wp = worldPos * tiling;

    float4 colX = tex.Sample(sam, wp.yz);
    float4 colY = tex.Sample(sam, wp.xz);
    float4 colZ = tex.Sample(sam, wp.xy);

    float3 blend = abs(normal);
    blend = pow(blend, g_BlendSharpness);
    blend /= (blend.x + blend.y + blend.z);

    return colX * blend.x + colY * blend.y + colZ * blend.z;
}

// ----------------------------------------------------------------------------
// Standard UV path
// ----------------------------------------------------------------------------
#else

PSInput VSMain(VSInput input)
{
    PSInput result;

    float4 pos = mul(input.position, world);
    result.position = mul(mul(pos, view), projection);
    result.worldPos = 0.0; // unused

    // world space normal – same transform as triplanar
    result.normal = normalize(mul(input.norm, (float3x3)world));

    result.uv = input.uv;
    return result;
}

#endif

// ----------------------------------------------------------------------------
// Pixel shader – common lighting for both paths
// ----------------------------------------------------------------------------
float4 PSMain(PSInput input) : SV_TARGET
{
    // Sample texture (path‑specific method)
#ifdef TRIPLANAR
    float4 texColor = SampleTriplanar(g_texture, g_sampler,
                                      input.worldPos, input.normal, g_Tiling);
#else
    float4 texColor = g_texture.Sample(g_sampler, input.uv);
#endif
    
    float3 N = normalize(input.normal);
    float3 L = normalize(light_direction.xyz);    
    float NdotL = saturate(dot(N, L));
    
    float3 final = texColor.rgb * ambient_colour.rgb
                 + texColor.rgb * light_colour.rgb * NdotL;

    return float4(final, texColor.a);
}