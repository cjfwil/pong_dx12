# Extracted TODOs

Generated on 2026-02-13 07:50:57


**Total files with TODOs:** 2  
**Total TODO entries:** 7

---

## `main.cpp`  (3)

### Line 134

**Line content:** `char *windowName = "window_name_todo_change";`

**Context:**

```
   131:     WindowMode m_currentMode;
   132:     SDL_Window *window = nullptr;
   133:     HWND hwnd = nullptr;
→  134:     char *windowName = "window_name_todo_change";
   135: 
   136:     HWND GetWindowHWND()
   137:     {
```

### Line 422

**Line content:** `// todo change this to a "scene" patter`

**Context:**

```
   419:     HRAssert(pipeline_dx12.m_swapChain->Present(syncInterval, syncFlags));
   420: }
   421: 
→  422: // todo change this to a "scene" patter
   423: static const int g_total_objects_count = 16;
   424: static struct
   425: {
```

### Line 426

**Line content:** `// todo: add ambient colour`

**Context:**

```
   423: static const int g_total_objects_count = 16;
   424: static struct
   425: {
→  426:     // todo: add ambient colour
   427:     // add one skybox set    
   428:     struct
   429:     {
```

---

## `src/renderer_dx12.h`  (4)

### Line 657

**Line content:** `// todo move this out`

**Context:**

```
   654:     if (!HRAssert(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&factory))))
   655:         return false;
   656: 
→  657:     // todo move this out
   658:     bool m_useWarpDevice = false;
   659:     if (m_useWarpDevice)
   660:     {
```

### Line 928

**Line content:** `// todo print the shader error on fail`

**Context:**

```
   925: #endif
   926:         ID3DBlob *shader_error_blob = nullptr;
   927: 
→  928:         // todo print the shader error on fail
   929:         if (FAILED(D3DCompileFromFile(L"shader_source\\shaders.hlsl", nullptr, nullptr, "VSMain", "vs_5_0", compileFlags, 0, &vertexShader, &shader_error_blob)))
   930:         {
   931:             if (shader_error_blob)
```

### Line 1085

**Line content:** `// todo: figure out if i am allowed to unmap?`

**Context:**

```
  1082:             cbvSrvDescriptorSize);
  1083:         pipeline_dx12.m_device->CreateConstantBufferView(&perSceneCbvDesc, perSceneCbvHandle);
  1084: 
→ 1085:         // todo: figure out if i am allowed to unmap?
  1086:         // todo: abstract this so i can call it from main when i want it updated
  1087:         // Map and initialize the per-scene constant buffer
  1088:         CD3DX12_RANGE readRange(0, 0);
```

### Line 1086

**Line content:** `// todo: abstract this so i can call it from main when i want it updated`

**Context:**

```
  1083:         pipeline_dx12.m_device->CreateConstantBufferView(&perSceneCbvDesc, perSceneCbvHandle);
  1084: 
  1085:         // todo: figure out if i am allowed to unmap?
→ 1086:         // todo: abstract this so i can call it from main when i want it updated
  1087:         // Map and initialize the per-scene constant buffer
  1088:         CD3DX12_RANGE readRange(0, 0);
  1089:         HRAssert(graphics_resources.m_PerSceneConstantBuffer->Map(
```

---
