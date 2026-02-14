# Extracted TODOs

Generated on 2026-02-13 14:34:38


**Total files with TODOs:** 3  
**Total TODO entries:** 4

---

## `main.cpp`  (1)

### Line 164

**Line content:** `char *windowName = "window_name_todo_change";`

**Context:**

```
   162:     SDL_Window *window = nullptr;
   163:     HWND hwnd = nullptr;
→  164:     char *windowName = "window_name_todo_change";
   165: 
   166:     HWND GetWindowHWND()
```

---

## `src/renderer_dx12.cpp`  (2)

### Line 657

**Line content:** `// todo move this out`

**Context:**

```
   655:         return false;
   656: 
→  657:     // todo move this out
   658:     bool m_useWarpDevice = false;
   659:     if (m_useWarpDevice)
```

### Line 1079

**Line content:** `// todo: abstract this so i can call it from main when i want it updated`

**Context:**

```
  1077:         pipeline_dx12.m_device->CreateConstantBufferView(&perSceneCbvDesc, perSceneCbvHandle);
  1078:         
→ 1079:         // todo: abstract this so i can call it from main when i want it updated
  1080:         // Map and initialize the per-scene constant buffer
  1081:         CD3DX12_RANGE readRange(0, 0);
```

---

## `src/scene_data.h`  (1)

### Line 17

**Line content:** `// todo add more fields here later (ambient colour, etc.)`

**Context:**

```
    15:     SceneObject objects[MAX_SCENE_OBJECTS];
    16:     int objectCount;
→   17:     // todo add more fields here later (ambient colour, etc.)
    18: } Scene;
```

---
