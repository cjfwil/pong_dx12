# Extracted TODOs

Generated on 2026-02-25 07:26:32


**Total files with TODOs:** 5  
**Total TODO entries:** 23

---

## `main.cpp`  (7)

### Line 170

**Line content:** `char *windowName = "window_name_todo_change";`

**Context:**

```
   168:     SDL_Window *window = nullptr;
   169:     HWND hwnd = nullptr;
→  170:     char *windowName = "window_name_todo_change";
   171: 
   172:     HWND GetWindowHWND()
```

### Line 389

**Line content:** `bool enableAlphaForSky = true; // TODO: make this per object`

**Context:**

```
   387: 
   388:         UINT psoIndex = msaa_state.m_enabled ? msaa_state.m_currentSampleIndex : 0;
→  389:         bool enableAlphaForSky = true; // TODO: make this per object
   390:         UINT activeBlendMode = (objectType == ObjectType::OBJECT_SKY_SPHERE && enableAlphaForSky) ? BLEND_ALPHA : BLEND_OPAQUE;
   391:         ID3D12PipelineState *currentPSO = pipeline_dx12.m_pipelineStates[pl][activeBlendMode][psoIndex];
```

### Line 431

**Line content:** `// todo unify outside this if statement`

**Context:**

```
   429:         else if (objectType == OBJECT_LOADED_MODEL)
   430:         {
→  431:             // todo unify outside this if statement
   432:             currentDrawConstants.heightmapIndex = graphics_resources.m_models[loadedModelIndex].textureIndex;            
   433:             pipeline_dx12.m_commandList[sync_state.m_frameIndex]->SetGraphicsRoot32BitConstants(0, sizeof(PerDrawRootConstants) / 4, &currentDrawConstants, 0);
```

### Line 498

**Line content:** `// TODO: update this function to group objects by rendering pipeline`

**Context:**

```
   496: 
   497: // this functions exists for a future where we will do more than just render the whole scene, this will include culling here
→  498: // TODO: update this function to group objects by rendering pipeline
   499: void FillDrawList()
   500: {
```

### Line 1191

**Line content:** `// TODO: Add further object types here (e.g., OBJECT_TERRAIN, OBJECT_DECAL) as needed`

**Context:**

```
  1189:             }
  1190:         }
→ 1191:         // TODO: Add further object types here (e.g., OBJECT_TERRAIN, OBJECT_DECAL) as needed
  1192:     }
  1193: 
```

### Line 1283

**Line content:** `// todo: when we load everything, make a big table that keeps track of everything we have loaded, filenames, objecttypes, and where it is placed`

**Context:**

```
  1281:     read_scene();
  1282: 
→ 1283:     // todo: when we load everything, make a big table that keeps track of everything we have loaded, filenames, objecttypes, and where it is placed
  1284:     // todo: do not load same filename more than once
  1285: 
```

### Line 1284

**Line content:** `// todo: do not load same filename more than once`

**Context:**

```
  1282: 
  1283:     // todo: when we load everything, make a big table that keeps track of everything we have loaded, filenames, objecttypes, and where it is placed
→ 1284:     // todo: do not load same filename more than once
  1285: 
  1286:     // new pattern?:
```

---

## `shader_source/shaders.hlsl`  (5)

### Line 11

**Line content:** `uint heightmapIndex; // TODO: rename this because in practice it is just an abstract index into albedo, heightmap or sky texture arrays`

**Context:**

```
     9: {
    10:     float4x4 world;
→   11:     uint heightmapIndex; // TODO: rename this because in practice it is just an abstract index into albedo, heightmap or sky texture arrays
    12:     float per_draw_padding[3];
    13: };
```

### Line 33

**Line content:** `Texture2D g_heightmaps[] : register(t1);    // todo place under heightfield define`

**Context:**

```
    31: 
    32: Texture2D g_defaultTexture : register(t0);
→   33: Texture2D g_heightmaps[] : register(t1);    // todo place under heightfield define
    34: Texture2D g_skyTextures[] : register(t257); // after heightmaps (t1..t256)
    35: Texture2D g_albedoTextures[] : register(t273); // because MAX_SKY_TEXTURES = 16 right now
```

### Line 37

**Line content:** `// TODO: METAPROGRAM all these register positions, or calculate with macros? doesn't matter just get rid of magic numbers`

**Context:**

```
    35: Texture2D g_albedoTextures[] : register(t273); // because MAX_SKY_TEXTURES = 16 right now
    36: 
→   37: // TODO: METAPROGRAM all these register positions, or calculate with macros? doesn't matter just get rid of magic numbers
    38: 
    39: 
```

### Line 56

**Line content:** `float3 worldPos : POSITION2; // world space position (triplanar) / unused. TODO: remove this or conditional compilation`

**Context:**

```
    54: {
    55:     float4 position : SV_POSITION;
→   56:     float3 worldPos : POSITION2; // world space position (triplanar) / unused. TODO: remove this or conditional compilation
    57:     float3 normal : NORMAL;      // world space normal (normalised)
    58:     float2 uv : TEXCOORD;
```

### Line 83

**Line content:** `// TODO: factor in non-uniform scale on normals`

**Context:**

```
    81: #endif
    82: 
→   83: // TODO: factor in non-uniform scale on normals
    84: PSInput VSMain(VSInput input)
    85: {
```

---

## `src/render_pipeline_data.h`  (1)

### Line 19

**Line content:** `// dithering blend mode? TODO: what other possible blend modes could we have?`

**Context:**

```
    17:     BLEND_OPAQUE = 0,
    18:     BLEND_ALPHA,
→   19:     // dithering blend mode? TODO: what other possible blend modes could we have?
    20:     BLEND_COUNT
    21: };
```

---

## `src/renderer_dx12.cpp`  (8)

### Line 112

**Line content:** `// TODO: metaprogram this so it is automatically correct?`

**Context:**

```
   110: #define MAX_SKY_TEXTURES 16
   111: 
→  112: // TODO: metaprogram this so it is automatically correct?
   113: namespace DescriptorIndices
   114: {
```

### Line 221

**Line content:** `// todo: unify this:`

**Context:**

```
   219:     PerSceneConstantBuffer m_PerSceneConstantBufferData;
   220: 
→  221:     // todo: unify this:
   222:     D3D12_VERTEX_BUFFER_VIEW m_vertexBufferView[PrimitiveType::PRIMITIVE_COUNT] = {};
   223:     D3D12_INDEX_BUFFER_VIEW m_indexBufferView[PrimitiveType::PRIMITIVE_COUNT] = {};
```

### Line 250

**Line content:** `// TODO move to the scene struct?`

**Context:**

```
   248:     ID3D12Resource *m_skyResources[MAX_SKY_TEXTURES] = {};
   249: 
→  250:     // TODO move to the scene struct?
   251:     UINT m_sceneObjectIndices[MAX_SCENE_OBJECTS] = {}; // per‑object texture index
   252:     UINT m_nextSceneObject = 0;                        // next free slot (starts at 0)
```

### Line 730

**Line content:** `// TODO: maybe unifiy with texture heap?`

**Context:**

```
   728: }
   729: 
→  730: // TODO: maybe unifiy with texture heap?
   731: struct ModelLoadResult
   732: {
```

### Line 1343

**Line content:** `// todo move this out`

**Context:**

```
  1341:         return false;
  1342: 
→ 1343:     // todo move this out
  1344:     bool m_useWarpDevice = false;
  1345:     if (m_useWarpDevice)
```

### Line 1600

**Line content:** `// TODO: Metaprogram ranges so it is automatically correct, unify with descriptorIndices struct`

**Context:**

```
  1598:         cbvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 2);
  1599: 
→ 1600:         // TODO: Metaprogram ranges so it is automatically correct, unify with descriptorIndices struct
  1601:         CD3DX12_DESCRIPTOR_RANGE srvRanges[4];
  1602:         srvRanges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
```

### Line 1923

**Line content:** `// todo: abstract this so i can call it from main when i want it updated`

**Context:**

```
  1921:         pipeline_dx12.m_device->CreateConstantBufferView(&perSceneCbvDesc, perSceneCbvHandle);
  1922: 
→ 1923:         // todo: abstract this so i can call it from main when i want it updated
  1924:         // Map and initialize the per-scene constant buffer
  1925:         CD3DX12_RANGE readRange(0, 0);
```

### Line 2122

**Line content:** `// todo: load from dds here, if not exist then make up one then write it, and reload it`

**Context:**

```
  2120:         std::vector<UINT8> hmData(hmWidth * hmHeight, 128); // mid grey
  2121: 
→ 2122:         // todo: load from dds here, if not exist then make up one then write it, and reload it
  2123:         for (int x = 0; x < hmWidth; ++x)
  2124:         {
```

---

## `src/scene_data.h`  (2)

### Line 48

**Line content:** `float choppiness; //example only, placeholder (todo implement water)`

**Context:**

```
    46:         } sky_sphere;
    47:         struct {
→   48:             float choppiness; //example only, placeholder (todo implement water)
    49:         } water;
    50:     } data;
```

### Line 56

**Line content:** `// todo add more fields here later (ambient colour, lights etc.)`

**Context:**

```
    54:     SceneObject objects[MAX_SCENE_OBJECTS];
    55:     int objectCount;
→   56:     // todo add more fields here later (ambient colour, lights etc.)
    57: };
```

---
