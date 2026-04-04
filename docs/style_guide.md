# Project Style & Best Practices

## Core Philosophy (Inspired my Casey Muratori and Mike Acton)

- **Procedural, not object-oriented** – Code is organised around data transformation, not objects with methods. Structs contain only data; functions operate on data.
- **No hidden state** – Dependencies are explicit. Functions receive necessary data via parameters (e.g., `EngineContext* ctx`) rather than reaching for globals.
- **Data‑oriented mindset** – Group related data into flat structs. Keep hot data together for cache locality.
- **Minimal abstraction** – Avoid deep call chains, virtual functions, and unnecessary indirection. Code should be straightforward to follow.

## Global State

- A single `EngineContext` struct holds all major rendering state (`pipeline_dx12`, `graphics_resources`, `sync_state`, `viewport_state`, `msaa_state`).
- Only **one global instance** of the context exists (`g_engine`). It is never copied or assigned.
- Other globals are used only when they truly represent single, system‑wide things (`g_input`, `g_camera`, `g_scene`, `g_bot_objects`).
- Functions that need part of the state take `EngineContext*` (or specific sub‑structs) as an explicit parameter.

## Struct Design

- Struct members are **public by default** – no getters/setters.
- Use `const` for read‑only arrays (e.g., `m_sampleCounts[4]`) to express intent, but be aware this deletes assignment operators – acceptable since instances are never assigned.
- Keep frequently‑accessed members near the top of the struct (cache friendly).
- Prefer fixed‑size arrays over `std::vector` where the maximum size is known at compile time.

## Naming Conventions

- **Global variables**: `g_` prefix (`g_engine`, `g_input`, `g_camera`).
- **Struct members**: `m_` prefix (`m_device`, `m_frameIndex`, `m_pipelineStates`).
- **Enums**: PascalCase, accessed via enum name (e.g., `PrimitiveType::PRIMITIVE_CUBE`).
- **Functions**: PascalCase (`UpdateBots`, `PopulateCommandList`).
- **Local variables**: lowerCamelCase or simple short names.

## Error Handling

- Use `HRAssert(hr)` for HRESULT checks – logs the error and breaks into the debugger on debug builds.
- For non‑critical failures, use `log_error`, `log_sdl_error`, or `log_hr_error`.
- Avoid raw `if (FAILED(hr))` blocks; wrap them with `HRAssert` or use the logging helpers directly.

## Metaprogramming

- Generate repetitive C++ code using Python scripts (e.g., `meta_ondestroy.py`, `meta_config.py`, `meta_mesh.py`).
- Keep generated files in `src/generated/` and never edit them manually.
- Generated code includes a clear header warning “DO NOT EDIT”.
- Use the generator scripts to avoid manual duplication (e.g., resource cleanup, config serialisation, mesh data).

## Memory Management

- Prefer flat arrays over dynamic containers (`std::vector` is permitted only when absolutely necessary).
- For GPU resources, use COM pointers and let `OnDestroy` (generated) release them in dependency order.
- CPU‑allocated buffers (e.g., `m_heightmapData`) must be manually freed in `OnDestroy` (or a dedicated cleanup function).
- Keep persistent mapped pointers (e.g., `m_pCbvDataBegin`) for constant buffers – map once, keep mapped.

## Rendering Patterns

- **Triple buffering** – All per‑frame resources (render targets, command allocators, constant buffers, fence values) have size `g_FrameCount = 3`.
- **Explicit state transitions** – Use `CD3DX12_RESOURCE_BARRIER` to transition resources between states.
- **MSAA support** – The pipeline array `m_pipelineStates[tech][blend][msaa]` stores PSOs for all combinations; the active MSAA level is selected at runtime.
- **Command list recording** – Each frame: reset allocator/list, set root signature/heaps, bind CBVs, transition back buffer, clear, draw, render ImGui, transition to present, close.

## Code Organisation

- **main.cpp** – Entry point, SDL event loop, ImGui, game logic (camera, bots, shooting).
- **src/renderer_dx12.cpp** – All DX12 rendering code (pipeline, resources, assets).
- **src/generated/** – Auto‑generated files; never edit manually.
- **src/*.h** – Headers for collision, ray intersections, scene data, etc.
- **shader_source/** – HLSL shaders, compiled at runtime.

## Avoid

- STL containers in hot paths (except `std::vector` as a temporary when loading assets).
- RTTI, exceptions, and heavy templates.
- Deep inheritance hierarchies.
- Hidden side effects in functions.
- Magic numbers – prefer named constants (e.g., `DescriptorIndices`, `RootParams`).

## Goal

The code should be **clear, explicit, and predictable**. A reader should be able to see where data comes from and where it goes without jumping between many files or guessing about hidden behaviour. Performance is a close second - aim for efficient memory access patterns and minimal CPU overhead.