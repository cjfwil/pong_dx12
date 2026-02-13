# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is a DirectX 12 graphics project using SDL3 for windowing and ImGui for UI. The project demonstrates modern DX12 rendering with triple buffering, constant buffers, and HLSL shaders.

## Build Commands

### Debug Build (VS Code default)
Uses the VS Code task configuration to build with debug symbols:
```bash
# The default VS Code task builds with /Zi /MDd for debug
# Press Ctrl+Shift+B in VS Code, or run:
cl.exe /D_DEBUG /Zi /EHsc /Wall /external:W0 /nologo /MDd /Isrc /Isrc/generated /Fe:main.exe main.cpp
```
Note: Include paths `/Isrc` and `/Isrc/generated` required for headers.

### Release Build
Use the Python build script:
```bash
python build.py
```

This creates an optimized executable in the `release/` folder and copies:
- Compiled executable
- SDL3.dll
- shader_source/ folder with HLSL shaders

### Line Count Statistics
```bash
python loc.py --verbose
```

## Architecture

### Code Organization

- **main.cpp**: Entry point, SDL initialization, main render loop, ImGui integration, scene management
- **src/renderer_dx12.h**: Complete DirectX 12 rendering pipeline (700+ lines)
  - Pipeline state, command objects, synchronization primitives
  - Resource creation and management
  - Contains `LoadPipeline()` and `LoadAssets()` functions
- **src/local_error.h**: Error handling utilities for HRESULT, SDL errors
- **src/config_ini_io.h**: Config struct definitions for INI serialization
- **shader_source/shaders.hlsl**: Vertex and pixel shaders
- **src/generated/OnDestroy_generated.cpp**: Auto-generated cleanup code
- **src/generated/config_functions.h**: Auto-generated INI read/write functions
- **src/generated/mesh_data.h**: Auto-generated primitive geometry data

### Global State Structures

The rendering system uses several global struct instances defined in `src/renderer_dx12.h`:

- **pipeline_dx12**: Core DX12 pipeline objects (device, command queue, swap chain, multiple pipeline states for MSAA, root signature, command list/allocators, descriptor heaps, MSAA render targets)
- **graphics_resources**: Per-frame constant buffers, vertex buffer, texture resources
- **sync_state**: Frame synchronization (fence, fence values, fence event, frame index)
- **msaa_state**: MSAA configuration (enabled flag, current sample count/index, supported levels, CalcSupportedMSAALevels helper)
- **viewport_state**: Window dimensions and aspect ratio
- **g_FrameCount**: Set to 3 for triple buffering

### Key DirectX 12 Patterns

#### Triple Buffering
The project uses triple buffering (`g_FrameCount = 3`) for all per-frame resources:
- Render targets (back buffers)
- Command allocators
- Constant buffers
- Fence values for synchronization

#### Synchronization Flow
1. **MoveToNextFrame()**: Signals fence, gets next back buffer index, waits if frame not ready
2. **WaitForGpu()**: Forces CPU to wait for all GPU work to complete
3. Each frame has its own fence value to track GPU completion

#### Resource Lifecycle
1. **LoadPipeline()**: Creates device, command queue, swap chain, descriptor heaps, depth buffer
2. **LoadAssets()**: Creates root signature, pipeline state, shaders, vertex/constant buffers, textures
3. **OnDestroy()**: Releases resources in dependency order (see Meta-Programming section)

#### Descriptor Heaps
- **m_rtvHeap**: Render target views (one per frame)
- **m_msaaRtvHeap**: MSAA render target views (when MSAA enabled)
- **m_dsvHeap**: Depth stencil view (single depth buffer)
- **m_mainHeap**: CBV/SRV heap for constant buffers and texture (shader-visible)
- **m_imguiHeap**: Separate heap for ImGui rendering

#### MSAA Support
Project supports 1x, 2x, 4x, 8x multisample anti-aliasing:
- **pipeline_dx12.m_pipelineStates[4]**: Array of PSOs, one per MSAA level
- **msaa_state.CalcSupportedMSAALevels()**: Queries device for supported sample counts
- When MSAA enabled, renders to MSAA targets then resolves to back buffer
- ResetCommandObjects() selects appropriate PSO based on current MSAA setting

#### Command Recording Pattern
Each frame in `PopulateCommandList()`:
1. Reset command allocator and command list
2. Set root signature and descriptor heaps
3. Bind per-frame constant buffer (indexed by frame)
4. Transition back buffer to render target state
5. Clear render target and depth buffer
6. Draw geometry
7. Render ImGui
8. Transition back buffer to present state
9. Close command list

### Meta-Programming

Project uses three Python scripts to generate C code:

#### 1. Resource Cleanup (`meta_ondestroy.py`)

**Important**: The `OnDestroy()` function is automatically generated.

**How It Works:**
1. Parses `src/renderer_dx12.h`
2. Extracts all COM pointer declarations (ID3D12*, IDXGI*)
3. Determines correct cleanup order based on D3D12 dependency rules
4. Generates `src/generated/OnDestroy_generated.cpp` with proper Release() calls
5. main.cpp includes this file with fallback if generation fails

**When to Regenerate:**
Run `python meta_ondestroy.py` whenever you:
- Add new D3D12 resources to pipeline_dx12, graphics_resources, or sync_state structs
- Change resource names
- Add new per-frame resource arrays

**Cleanup Order:**
Resources are released in this priority order:
1. Sync objects (fence)
2. Constant buffers (with Unmap calls)
3. Graphics resources (textures, buffers, depth buffer)
4. Pipeline objects (command list, pipeline state, root signature)
5. Per-frame resources (render targets, command allocators)
6. Descriptor heaps
7. Swap chain
8. Command queue
9. Device (last)

#### 2. Config I/O (`meta_config.py`)

Generates INI file serialization code for runtime settings.

**How It Works:**
1. Parses `ConfigData` struct in `src/config_ini_io.h`
2. Generates `src/generated/config_functions.h` with INI read/write functions
3. Handles nested structs (DisplaySettings, GraphicsSettings)
4. Creates INI sections automatically from struct member names

**When to Regenerate:**
Run `python meta_config.py` whenever you:
- Add new config fields to ConfigData
- Add new nested config sections
- Change config field names or types

**Config Persistence:**
- Settings saved to `config.ini` in working directory
- Automatically loads on startup, creates default if missing
- SaveConfig() called after user changes settings in ImGui
- Stores: window dimensions, window mode, MSAA level, vsync

#### 3. Mesh Generation (`meta_mesh.py`)

Generates primitive geometry data with vertex normals for rendering.

**How It Works:**
1. Defines geometry generators (cube, cylinder, prism, sphere, inverted sphere)
2. Each generator computes positions, normals, and UVs
3. Generates `src/generated/mesh_data.h` with vertex/index arrays and lookup tables
4. Creates `PrimitiveType` enum matching array order

**When to Regenerate:**
Run `python meta_mesh.py` whenever you:
- Add new primitive shapes
- Change vertex format (currently: position, normal, uv)
- Modify geometry parameters (slice counts, dimensions)

**Primitives:**
- **Cube**: 24 vertices (hard edges, one normal per face)
- **Cylinder**: Configurable slices, smooth sides, flat caps
- **Prism**: Triangular prism with hard edges
- **Sphere**: UV sphere with smooth normals
- **Inverted Sphere**: Same as sphere but normals point inward (for skyboxes)

**Never manually modify** generated files in `src/generated/` - your changes will be overwritten.

## Known Issues

See `docs/bugs.md` for current bug tracking (currently empty - no active bugs).

## Scene Management

Scene objects stored in binary format (`scene.bin`):
- Read/write via custom serialization
- Objects have position, rotation, scale, primitive type
- ImGuizmo provides 3D gizmo manipulation in editor
- Scene persists between runs

**ImGuizmo Integration:**
- Linked via ImGuizmo.lib (separate debug/release builds)
- Provides translate/rotate/scale gizmos for selected objects
- Gizmos rendered in main loop after ImGui UI

## Design Decisions

Key architectural choices (see `docs/decisions.md`):

**Display Modes:**
- No exclusive fullscreen support
- Internal resolution scaler: 50%-200% (planned)
- Windowed mode: preset resolutions (4:3, 16:9, 21:9)
- Borderless: always desktop resolution

## Code Cleanup Reference

`architecture_problems.md` documents 15 specific areas for reducing duplication and implicit dependencies while maintaining procedural style. Reference when refactoring.

## Shader Compilation

Shaders are compiled at runtime from `shader_source/shaders.hlsl`:
- Vertex shader entry point: `VSMain` (vs_5_0)
- Pixel shader entry point: `PSMain` (ps_5_0)
- Debug builds use `/D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION`
- Release builds compile without debug flags

The shader file must be present in the working directory when running the executable.

## ImGui Integration

ImGui uses a custom descriptor allocator (`g_imguiHeap` struct in main.cpp) that:
- Manages a pool of 10 descriptor slots
- Provides allocation/free callbacks to ImGui backend
- Uses separate descriptor heap from main rendering

## Dependencies

All dependencies are linked via pragma comment directives in main.cpp:
- **SDL3.lib**: Windowing and input (SDL3.dll required at runtime)
- **DirectX 12**: d3d12.lib, dxgi.lib, d3dcompiler.lib, dxcompiler.lib, dxguid.lib
- **ImGui**: imgui.lib (UI framework)
- **ImGuizmo.lib**: 3D gizmo manipulation (debug/release variants)
- **DirectXTex**: Texture loading (DirectXTex.lib debug, DirectXTex_release.lib release)
- **cgltf**: GLTF model loading (header-only, included in main.cpp)

SDL3.dll must be available at runtime (copied by build.py for release builds).

## Debugging

- Error handling uses custom `HRAssert()` macro that logs HRESULT errors and breaks into debugger on debug builds
- All errors logged via SDL_Log
- Window title shows performance metrics (FPS, frame time)

## Instructions to Claude

Answer all questions concisely. Sacrifice grammar as much as you can get away with, so we can get our outputs as quickly readable as possible.