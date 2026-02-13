# Code Cleanup Plan: Abstracting Common Patterns

This document lists specific areas of the codebase where duplication, scatter, or implicit dependencies can be reduced. Each entry describes the **problem**, **where** it appears, and a concrete **solution** (without code) that removes repetition, centralises logic, or makes dependencies explicit – all while staying within a procedural, non‑OOP style.

## Summary

These fifteen clean‑up points address the most obvious duplication, scattered responsibilities, and implicit dependencies in the codebase. Each solution stays within a procedural, non‑OOP style – using data tables, helper functions, and explicit context passing. Implementing them will make the code shorter, more maintainable, and easier to extend without introducing class hierarchies or “Clean Code” dogma.

---

## 1. Build script – folder & file copying

**Problem**  
The `build.py` script contains four near‑identical blocks for copying directories (`shader_source`, `assets`), DLLs, and `scene.bin`. Each block checks existence, deletes the destination if it exists, then copies. This is repetitive and makes adding another asset error‑prone.

**Where**  
`build.py` – the sections labelled “Copy shader folder”, “Copy assets folder”, “Copy required DLLs”, “Copy scene.bin”.

**Solution**  
Create a single helper function `copy_to_release(src)` that:
- Takes a path (file or directory).
- Computes the destination inside `OUTPUT_DIR`.
- If the destination exists, removes it (`rmtree` for dir, `unlink` for file).
- Copies (tree or single file) and prints a status message.
Then the main build calls this function for each item to copy.

---

## 2. Meta‑script CLI boilerplate

**Problem**  
Each meta‑script (`meta_config.py`, `meta_mesh.py`, `meta_ondestroy.py`) repeats the same `argparse` setup, `if __name__ == '__main__'`, and call to its generator. Adding a new meta‑tool requires copy‑pasting this glue.

**Where**  
`meta_config.py`, `meta_mesh.py`, `meta_ondestroy.py` – the `if __name__ == '__main__'` blocks.

**Solution**  
Move the common argument‑parsing and execution flow into `common.py` as a function `run_generator(generator_func, description)`. Each meta‑script then only defines its generator function and calls `common.run_generator(...)`. The generator function should accept `(input_path, output_path, force)`.

---

## 3. solved

---

## 4. DX12 resource creation – common patterns

**Problem**  
Creating `ID3D12Resource` buffers with upload heaps follows the same pattern in multiple places:  
- Vertex buffers, index buffers, constant buffers, texture uploads.  
Each repeats: `CreateCommittedResource` for default heap, `CreateCommittedResource` for upload heap, `Map`, `memcpy`, `Unmap`, `CopyBufferRegion`, barrier.  
Also, render targets and depth buffers are created with similar `CreateCommittedResource` calls, differing only in the descriptor.

**Where**  
`renderer_dx12.h` – `CreatePrimitiveMeshBuffers`, texture loading in `LoadAssets`, and the resource recreation code in `RecreateSwapChain` and `CreateMSAAResources`.

**Solution**  
Write a small set of focused helper functions:
- `ID3D12Resource* CreateDefaultBuffer(device, cmdList, data, size, targetState, uploadBuffer*)` – encapsulates the entire upload process.
- `void CreateRenderTarget(device, width, height, format, sampleCount, clearValue, resource*)` – handles the common desc setup.
- `void CreateDepthBuffer(device, width, height, sampleCount, clearValue, resource*)`.  
These functions hide the repetitive heap properties, resource descs, and state transitions.

---

## 5. Descriptor handle arithmetic

**Problem**  
Many places manually compute `CD3DX12_CPU_DESCRIPTOR_HANDLE` and `CD3DX12_GPU_DESCRIPTOR_HANDLE` by offsetting with an index and descriptor size. This is verbose and error‑prone, especially when indices are symbolic constants.

**Where**  
`PopulateCommandList` (MSAA RTV, depth, back buffer), `LoadPipeline` (RTV creation), `LoadAssets` (CBV and SRV placement), and others.

**Solution**  
Provide simple inline helpers (or macros) that take a heap, an index, and the (already stored) descriptor increment, and return the appropriate handle.  
Example: `CpuHandleFromIndex(heap, index, increment)` and `GpuHandleFromIndex(heap, index, increment)`.  
Store the increment sizes once in the pipeline struct and reuse them.

---

## 6. Constant buffer updating

**Problem**  
`Update()` does a manual `memcpy` to the mapped per‑frame constant buffer. The per‑scene constant buffer is updated via another explicit `memcpy` when ambient colour changes. `todo.md` explicitly notes that this pattern will be needed again and should be abstracted.

**Where**  
`main.cpp` – `Update()` and the ambient‑color ImGui handler; `LoadAssets` – the initial mapping and copy.

**Solution**  
Create a function `UpdateConstantBuffer(buffer, data, size, mappedPtr)` that:
- If a persistent mapped pointer is supplied and non‑null, uses it.
- Otherwise maps the buffer, copies, and optionally unmaps (or keeps mapped if pointer is stored).
This function centralises the `Map`/`memcpy` logic and eliminates the repeated `CD3DX12_RANGE(0,0)` boilerplate.

---

## 7. Shader compilation error handling

**Problem**  
In `LoadAssets()`, the vertex and pixel shader compilation blocks are almost identical: `D3DCompileFromFile`, error blob logging, and `HRAssert`. Duplicate code.

**Where**  
`renderer_dx12.h` – `LoadAssets()`, the two `D3DCompileFromFile` calls.

**Solution**  
Write a helper `bool CompileShader(path, entry, target, blobOut)` that:
- Calls `D3DCompileFromFile` with the appropriate debug/release flags.
- If it fails, logs the error blob (if any) and returns `false` via `HRAssert`.
- On success, returns `true` and fills `blobOut`.
Then `LoadAssets()` simply calls it twice.

---

## 8. ImGui UI sprawl

**Problem**  
The main loop in `main.cpp` contains a very long block of ImGui calls (the “Settings” window, “Scene Objects” window, gizmo rendering) mixed with event handling and state updates. This makes the loop hard to read and maintain.

**Where**  
`main.cpp` – from `ImGui::Begin("Settings")` down to the end of the scene‑objects window.

**Solution**  
Split each major UI component into its own function that takes the relevant global state (by pointer) and renders the ImGui controls:
- `DrawSettingsWindow(window_state*, msaa_state*, config*, ...)`
- `DrawSceneObjectsWindow(object_list*, selected_index*)`
- `DrawGizmo(selected_object*, camera_params*)`  
The main loop then calls these functions sequentially, dramatically shortening the loop and isolating UI logic.

---

## 9. Window mode request handling

**Problem**  
Window mode changes are requested via a separate global `window_request` struct, and `ApplyWindowMode()` reads that request. The request and the current state are separate; the flow is spread across the event handler and the main loop.

**Where**  
`main.cpp` – global `window_request`; the ImGui combo that sets it; the main loop that checks and applies it; `window_state::ApplyWindowMode()`.

**Solution**  
Make the window‑mode transition a single operation: `RequestWindowMode(newMode)` that immediately performs the mode change (or schedules it if it cannot be done inside the event callback). Remove the separate “apply” flag and the delayed check. The function would call `ApplyWindowMode` directly, updating the window, swap chain, and config. This eliminates the intermediate request state.

---

## 10. MSAA combo box – table‑driven

**Problem**  
The MSAA selection UI in the “Settings” window has a custom combo that manually handles the “Disabled” case, checks supported flags, and builds item labels. It is lengthy and will need changes if MSAA levels are added or removed.

**Where**  
`main.cpp` – the `ImGui::BeginCombo("Anti-aliasing", ...)` block.

**Solution**  
Define a static array of `MsaaOption` structures, each containing:
- Display name (e.g., “4x MSAA”)
- Sample count
- Pointer to the `m_supported` flag
- Index into the state arrays.
Then loop over this array to generate the combo items. This removes the special‑case “Disabled” branch and the repeated `snprintf` calls. Changes to MSAA levels only require editing the array.

---

## 11. Draw list and object list – manual copying

**Problem**  
`g_total_objects_list_editable` holds the master scene objects; `g_draw_list` holds the flattened array of transforms and types for rendering. `FillDrawList()` manually copies each field one by one. This is repetitive and couples the two structures.

**Where**  
`main.cpp` – the `FillDrawList()` function.

**Solution**  
Either:
- Merge the two lists – use `g_total_objects_list_editable.objects` directly for rendering (the draw list is essentially a subset, but currently it just copies the first N objects). The render loop could iterate over the master list up to its count, removing the need for a separate draw list.
- Or, if the draw list must remain separate, write a single `memcpy`-style function that copies whole array of structs (the objects are already contiguous). The copy could be a single `memcpy` of `sizeof(objects)` bytes, then adjust the count.

---

## 12. Global state – implicit dependencies

**Problem**  
Many functions (e.g., `PopulateCommandList`, `Update`, `Render`, `RecreateSwapChain`) access global structs directly (`pipeline_dx12`, `sync_state`, `graphics_resources`, `viewport_state`, `msaa_state`). This makes dependencies implicit and hinders unit testing or future refactoring.

**Where**  
Throughout `renderer_dx12.h` and `main.cpp`.

**Solution**  
Group the major global states into a single “context” struct (plain C struct, no methods) that is passed as a pointer to functions that need it. For example:

```c
typedef struct {
    pipeline_dx12_t pipeline;
    sync_state_t sync;
    graphics_resources_t resources;
    viewport_state_t viewport;
    msaa_state_t msaa;
    // ... maybe window_state, config, etc.
} renderer_t;
```

Then each function receives `renderer_t* ctx`. This does not introduce classes – it simply makes the data flow explicit. The global instances can remain (e.g., `static renderer_t g_renderer`) but functions no longer reach out to unrelated globals.

---

## 13. Resource cleanup – missing constant buffer unmapping

**Problem**  
`OnDestroy_generated.cpp` releases COM pointers but does **not** `Unmap` the per‑frame or per‑scene constant buffers before releasing them. This is a resource leak (though often benign with process exit). The generator’s priority map includes a special case for constant buffers but currently only generates unmapping for those named `constantBuffer`; it does not match `m_PerFrameConstantBuffer` or `m_PerSceneConstantBuffer`.

**Where**  
`meta_ondestroy.py` – the `PRIORITY_MAP` and the code generation branch for constant buffers.

**Solution**  
Extend the constant‑buffer detection in `meta_ondestroy.py` to recognise `m_PerFrameConstantBuffer` and `m_PerSceneConstantBuffer` by name or by checking the struct member type (ID3D12Resource) and the fact that it was mapped. Generate an `Unmap(0, nullptr)` call before `Release()` for each such buffer. Also update the priority ordering to ensure unmapping happens before device release.

---

## 14. Error handling consistency

**Problem**  
`HRAssert` is used for most HRESULT checks, but sometimes its return value is ignored, and in other places the condition is checked manually. Logging is also done via both `SDL_Log` and the `log_*` functions.

**Where**  
Throughout `renderer_dx12.h` and `main.cpp`.

**Solution**  
Establish a simple policy:
- Use `HRAssert(hr)` for all `HRESULT` checks where failure should break into the debugger on debug builds and always log.
- Use `if (!HRAssert(hr)) return false;` for critical failures that should abort the current operation.
- Remove raw `if (FAILED(hr))` blocks where they just log and return – replace with the `HRAssert` pattern.
- Standardise logging: prefer the `log_*` helpers over raw `SDL_Log` for consistency.

---

## 15. Descriptor heap enumeration – symbolic indices

**Problem**  
Descriptor indices are defined in `DescriptorIndices` namespace, but many places still use raw numbers (e.g., `2` for the per‑scene CBV table, `3` for the SRV table) or manually compute offsets. This makes the code fragile if the layout changes.

**Where**  
`PopulateCommandList` – setting root descriptor tables; `LoadAssets` – placing the per‑scene CBV and SRV in the heap.

**Solution**  
Always use the symbolic constants from `DescriptorIndices`. Replace magic numbers with the named constants.  
If needed, add constants for the table indices themselves (e.g., `ROOT_PARAM_CBV_TABLE = 2`). This centralises the layout definition and makes reordering trivial.