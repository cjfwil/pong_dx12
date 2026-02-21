# Extracted TODOs

Generated on 2026-02-21 03:32:06


**Total files with TODOs:** 4  
**Total TODO entries:** 8

---

## `main.cpp`  (1)

### Line 168

**Line content:** `char *windowName = "window_name_todo_change";`

**Context:**

```
   166:     SDL_Window *window = nullptr;
   167:     HWND hwnd = nullptr;
→  168:     char *windowName = "window_name_todo_change";
   169: 
   170:     HWND GetWindowHWND()
```

---

## `shader_source/shaders.hlsl`  (2)

### Line 33

**Line content:** `Texture2D g_heightmaps[] : register(t1); // todo place under heightfield define`

**Context:**

```
    31: 
    32: Texture2D g_texture : register(t0);
→   33: Texture2D g_heightmaps[] : register(t1); // todo place under heightfield define
    34: SamplerState g_sampler : register(s0);
    35: // add a separate sampler for sampling heightfield?
```

### Line 77

**Line content:** `// TODO: factor in non-uniform scale on normals`

**Context:**

```
    75: #endif
    76: 
→   77: // TODO: factor in non-uniform scale on normals
    78: PSInput VSMain(VSInput input)
    79: {
```

---

## `src/renderer_dx12.cpp`  (3)

### Line 870

**Line content:** `// todo move this out`

**Context:**

```
   868:         return false;
   869: 
→  870:     // todo move this out
   871:     bool m_useWarpDevice = false;
   872:     if (m_useWarpDevice)
```

### Line 1387

**Line content:** `// todo: abstract this so i can call it from main when i want it updated`

**Context:**

```
  1385:         pipeline_dx12.m_device->CreateConstantBufferView(&perSceneCbvDesc, perSceneCbvHandle);
  1386: 
→ 1387:         // todo: abstract this so i can call it from main when i want it updated
  1388:         // Map and initialize the per-scene constant buffer
  1389:         CD3DX12_RANGE readRange(0, 0);
```

### Line 1586

**Line content:** `// todo: load from dds here, if not exist then make up one then write it, and reload it`

**Context:**

```
  1584:         std::vector<UINT8> hmData(hmWidth * hmHeight, 128); // mid grey
  1585: 
→ 1586:         // todo: load from dds here, if not exist then make up one then write it, and reload it
  1587:         for (int x = 0; x < hmWidth; ++x)
  1588:         {
```

---

## `src/scene_data.h`  (2)

### Line 47

**Line content:** `float choppiness; //example only, placeholder (todo implement water)`

**Context:**

```
    45:         } sky_sphere;
    46:         struct {
→   47:             float choppiness; //example only, placeholder (todo implement water)
    48:         } water;
    49:     } data;
```

### Line 55

**Line content:** `// todo add more fields here later (ambient colour, lights etc.)`

**Context:**

```
    53:     SceneObject objects[MAX_SCENE_OBJECTS];
    54:     int objectCount;
→   55:     // todo add more fields here later (ambient colour, lights etc.)
    56: };
```

---
