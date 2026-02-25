// common.hlsl – Unified shader with directional lighting
// Compile with -D TRIPLANAR for triplanar shading, otherwise standard UV mapping.
// Both paths now compute world‑space normals and support a directional light.

// ----------------------------------------------------------------------------
// Constant buffers
// ----------------------------------------------------------------------------
cbuffer PerDrawRootConstants : register(b0)
{
    float4x4 world;
    uint heightmapIndex; // TODO: rename this because in practice it is just an abstract index into albedo, heightmap or sky texture arrays
    float per_draw_padding[3];
};

cbuffer PerFrameConstantBuffer : register(b1)
{
    float4x4 view;
    float4x4 projection;
    float per_frame_padding[16 + 16]; // 256‑byte alignment
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

Texture2D g_defaultTexture : register(t0);
Texture2D g_heightmaps[] : register(t1);    // todo place under heightfield define
Texture2D g_skyTextures[] : register(t257); // after heightmaps (t1..t256)
Texture2D g_albedoTextures[] : register(t273); // because MAX_SKY_TEXTURES = 16 right now

// TODO: METAPROGRAM all these register positions, or calculate with macros? doesn't matter just get rid of magic numbers


SamplerState g_sampler : register(s0);
// add a separate sampler for sampling heightfield?

// ----------------------------------------------------------------------------
// Common vertex input / output
// ----------------------------------------------------------------------------
struct VSInput
{
    float4 position : POSITION;
    float3 norm : NORMAL;
    float2 uv : TEXCOORD;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float3 worldPos : POSITION2; // world space position (triplanar) / unused. TODO: remove this or conditional compilation
    float3 normal : NORMAL;      // world space normal (normalised)
    float2 uv : TEXCOORD;
};

#ifdef TRIPLANAR
// Triplanar sampling parameters
static const float g_Tiling = 1.0f;
static const float g_BlendSharpness = 16.0f;

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
#endif

// TODO: factor in non-uniform scale on normals
PSInput VSMain(VSInput input)
{
    PSInput result;

#ifdef HEIGHTFIELD
    float h = g_heightmaps[heightmapIndex].SampleLevel(g_sampler, input.uv, 0).r;
    float3 worldNormal = normalize(mul(input.norm, (float3x3)world));    
    float3 displacedPos = input.position.xyz + worldNormal * h;
    
    float4 finalWorldPos = mul(float4(displacedPos, 1.0f), world);
    result.position = mul(mul(finalWorldPos, view), projection);
    result.worldPos = finalWorldPos.xyz;
    result.normal = worldNormal;
    // result.uv = input.uv;
    result.uv = float2(h, h);
#elif defined(TRIPLANAR)    
    float4 worldPosition = mul(input.position, world);
    result.position = mul(mul(worldPosition, view), projection);
    result.worldPos = worldPosition.xyz;
    result.normal = normalize(mul(input.norm, (float3x3)world));
    result.uv = input.uv;

#else    
    float4 pos = mul(input.position, world);
    result.position = mul(mul(pos, view), projection);
    result.worldPos = 0.0; // unused
    result.normal = normalize(mul(input.norm, (float3x3)world));
    result.uv = input.uv;
#endif

    return result;
}

float4 PSMain(PSInput input) : SV_TARGET
{
#ifdef SKY
    float4 texColor = g_skyTextures[heightmapIndex].Sample(g_sampler, input.uv);
    return texColor;
#elif defined(HEIGHTFIELD)
    float4 texColor = float4(input.uv, input.uv.x, 1.0f);
#elif defined(TRIPLANAR)
    float4 texColor = SampleTriplanar(g_defaultTexture, g_sampler,
                                      input.worldPos, input.normal, g_Tiling);
#else
    float4 texColor = g_defaultTexture.Sample(g_sampler, input.uv);
#endif

    float3 N = normalize(input.normal);
    float3 L = normalize(light_direction.xyz);
    float NdotL = saturate(dot(N, L));

    float3 final = texColor.rgb * ambient_colour.rgb + texColor.rgb * light_colour.rgb * NdotL;

    // test colour
    // return float4(1, 0, 0, 1);
    return float4(final, texColor.a);
}
