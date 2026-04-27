# Canvas

A personal sandbox for exploring the layered design of a real-time 3D graphics engine, from high-level API surfaces down to GPU resource management.

## Overview

Canvas is an experimental engine built to study how the different layers of a modern rendering stack fit together:

- **API design**: how should scene graphs, materials, and rendering queues be expressed so that application code stays clean while the backend has room to optimise?
- **Resource lifecycle**: how do pooled buffers, placed allocations, upload rings, and multi-timeline fences cooperate to keep the GPU fed without leaking or stalling?
- **GPU work scheduling**: how can a task-graph automatically derive barriers and synchronisation from declared resource usage?
- **Rendering techniques**: deferred shading with a G-buffer, PBR materials, SDF text, and UI composition.

Canvas is *not* a production engine. It is a learning vehicle whose value lies in the patterns explored, not in feature completeness.

## Architecture

| Layer                    | Description                                                                                                                                  |
|--------------------------|----------------------------------------------------------------------------------------------------------------------------------------------|
| **Applications**         | CanvasModelViewer, CanvasConsole                                                                                                             |
| **CanvasCore**           | Scene graph, Cameras, Lights, Mesh instances, UI graph, Font / text layout, Math library                                                     |
| **Graphics Backend**     | Plugin, loaded at runtime. GPU task graph, Resource manager, Render queue, Copy queue, Upload ring, Deferred renderer                        |
| **Supporting Libraries** | CanvasText, CanvasFbx, RectanglePacker                                                                                                       |

### CanvasCore

A shared library that defines every public Canvas interface and implements API-agnostic logic. CanvasCore has **no dependency on any graphics API**; all GPU-facing work is delegated to a backend loaded as a plugin at runtime.

Key concepts:

| Concept                             | Description                                                                                                                                                        |
|-------------------------------------|--------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| **XCanvas**                         | Root object and factory.  Creates scenes, cameras, lights, meshes, fonts, and UI graphs.                                                                           |
| **XSceneGraph / XSceneGraphNode**   | Hierarchical transform graph.  Nodes carry local rotation (quaternion), translation, and scale; global transforms are lazily computed with dirty-flag propagation. |
| **XCamera**                         | Perspective camera (FOV, near/far clip, aspect ratio, exposure compensation).  Attaches to a scene-graph node for positioning.                                     |
| **XLight**                          | Point, directional, spot, ambient, and area light types with attenuation and spot-angle parameters.                                                                |
| **XMeshInstance**                   | Binds GPU mesh data to a scene-graph node for rendering.                                                                                                           |
| **XUIGraph / XUIGraphNode**         | 2-D UI overlay graph with position inheritance and dirty-tracked update.                                                                                           |
| **XUITextElement / XUIRectElement** | Text (SDF-rendered) and filled-rectangle HUD primitives. Created via `XGfxDevice`.                                                                                 |
| **XFont**                           | TrueType font resource providing metric access (ascender, descender, units-per-em).                                                                                |

All interfaces use the **GEM** object model, a lightweight COM-style system with reference counting, `QueryInterface`, and 64-bit interface IDs. This gives Canvas a stable ABI boundary between core and backend without pulling in COM infrastructure.

### CanvasGfx12 graphics backend

The only backend that currently exists. It is loaded as a DLL plugin via `XCanvas::LoadPlugin` and exposes `XGfxDevice`, from which all GPU objects are created. Notable subsystems:

| Subsystem             | Purpose                                                                                                                                                 |
|-----------------------|---------------------------------------------------------------------------------------------------------------------------------------------------------|
| **GPU Task Graph**    | DAG-based automatic barrier management using Enhanced Barriers.  Tasks declare resource usage; the graph resolves transitions.                          |
| **Resource Manager**  | Placed allocations, power-of-2 bucketed buffer pool, and multi-timeline deferred release (fence tokens across render/copy queues).                      |
| **Render Queue**      | Orchestrates frame rendering: G-buffer geometry pass, deferred lighting, UI overlay, and presentation.                                                  |
| **Copy Queue**        | Asynchronous upload pipeline for mesh vertex data and textures.  Cross-queue fence synchronisation makes uploads visible before the next render submit. |
| **Upload Ring**       | Per-queue linear ring buffer on the upload heap for per-frame constant and staging data.                                                                |
| **Deferred Renderer** | 5-render-target G-buffer with an uber-shader, PBR material binding, and a fullscreen composition pass.                                                  |

### CanvasMath

A header-only math library (`CanvasMath.hpp`) providing templated vectors, matrices, and quaternions. Key types include `FloatVector2`, `FloatVector3`, `FloatVector4`, `FloatMatrix4x4`, and `FloatQuaternion`. Utility functions cover dot/cross products, matrix multiplication, quaternion-to-matrix conversion, perspective projection, and basis construction (`ComposePointToBasisVectors`).

### Supporting Libraries

| Library             | Description                                                                                                                      |
|---------------------|----------------------------------------------------------------------------------------------------------------------------------|
| **CanvasText**      | TrueType font parsing and signed-distance-field (SDF) glyph rasterisation.                                                       |
| **CanvasFbx**       | FBX scene import (meshes, node hierarchies, materials, UVs, tangents) via the [ufbx](https://github.com/bqqbarbhg/ufbx) library. |
| **RectanglePacker** | Bin-packing algorithm used for texture atlas layout.                                                                             |

### Applications

| Application           | Description                                                                                                                     |
|-----------------------|---------------------------------------------------------------------------------------------------------------------------------|
| **CanvasModelViewer** | Windowed viewer application.  Loads FBX models, renders with PBR deferred shading, SDF text HUD, and FPS-style camera controls. |
| **CanvasConsole**     | Lightweight console harness for API smoke-testing without a window.                                                             |

## Getting Started

The snippet below shows the minimal sequence to bootstrap a Canvas scene with a camera and a mesh. Error handling is abbreviated for clarity; production code should check every `Gem::Result`.

```cpp
#include "CanvasCore.h"
#include "CanvasGfx.h"

// 1. Create the Canvas root object
Gem::TGemPtr<Canvas::XCanvas> pCanvas;
Canvas::CreateCanvas(pLogger, &pCanvas);

// 2. Load a graphics backend plugin
Gem::TGemPtr<Canvas::XCanvasPlugin> pPlugin;
pCanvas->LoadPlugin("CanvasGfx12.dll", &pPlugin);

// 3. Create a graphics device
Gem::TGemPtr<Canvas::XGfxDevice> pDevice;
pPlugin->CreateCanvasElement(
    pCanvas, Canvas::TypeId::TypeId_GfxDevice,
    "MyDevice", Canvas::XGfxDevice::IId,
    reinterpret_cast<void**>(&pDevice));

// 4. Create a render queue and swap chain
Gem::TGemPtr<Canvas::XGfxRenderQueue> pRenderQueue;
pDevice->CreateRenderQueue(&pRenderQueue);

Gem::TGemPtr<Canvas::XGfxSwapChain> pSwapChain;
pRenderQueue->CreateSwapChain(
    hWnd, true, &pSwapChain,
    Canvas::GfxFormat::R16G16B16A16_Float, 4);

// 5. Build a scene graph
Gem::TGemPtr<Canvas::XSceneGraph> pScene;
pCanvas->CreateSceneGraph(pDevice, &pScene, "MyScene");

// 6. Add a camera
Gem::TGemPtr<Canvas::XCamera> pCamera;
pCanvas->CreateCamera(&pCamera, "MainCamera");
pCamera->SetFovAngle(static_cast<float>(Canvas::Math::Pi / 4));
pCamera->SetNearClip(0.1f);
pCamera->SetFarClip(1000.f);

Gem::TGemPtr<Canvas::XSceneGraphNode> pCameraNode;
pCanvas->CreateSceneGraphNode(&pCameraNode, "CameraNode");
pCameraNode->BindElement(pCamera);
pCameraNode->SetLocalTranslation({0.f, -5.f, 2.f, 0.f});
pScene->GetRootSceneGraphNode()->AddChild(pCameraNode);
pScene->SetActiveCamera(pCamera);

// 7. Add a mesh
Gem::TGemPtr<Canvas::XGfxMeshData> pMeshData;
pDevice->CreateMeshData(vertexCount, positions, normals, &pMeshData, "Cube");

Gem::TGemPtr<Canvas::XMeshInstance> pMeshInstance;
pCanvas->CreateMeshInstance(&pMeshInstance, "CubeInstance");
pMeshInstance->SetMeshData(pMeshData);

Gem::TGemPtr<Canvas::XSceneGraphNode> pMeshNode;
pCanvas->CreateSceneGraphNode(&pMeshNode, "CubeNode");
pMeshNode->BindElement(pMeshInstance);
pScene->GetRootSceneGraphNode()->AddChild(pMeshNode);

// 8. Render loop
while (running)
{
    pScene->Update(deltaTime);
    pRenderQueue->BeginFrame(pSwapChain);
    pScene->SubmitRenderables(pRenderQueue);
    pRenderQueue->EndFrame();
    pRenderQueue->FlushAndPresent(pSwapChain);
}
```

## Key Design Decisions

### GEM Interface Model

Canvas objects expose functionality through **GEM interfaces**, a COM-inspired system that provides reference counting (`AddRef` / `Release`), interface discovery (`QueryInterface`), and 64-bit interface IDs. Smart pointers (`Gem::TGemPtr<T>`) manage lifetimes automatically.

Interfaces are declared with `GEM_INTERFACE_DECLARE` and mapped in concrete classes via `BEGIN_GEM_INTERFACE_MAP` / `GEM_INTERFACE_ENTRY` macros. This gives Canvas a binary-stable ABI boundary and makes the plugin architecture possible.

### Plugin Architecture

Graphics backends are loaded at runtime as shared libraries. The host calls `XCanvas::LoadPlugin(path)`, which loads the library and calls its exported `CreateCanvasPlugin` factory. This keeps CanvasCore free of any graphics-API dependency and opens the door to alternative backends (Vulkan, Metal, ...) in the future.

## Conventions

### Row-Vector Matrix Convention

All transforms use row vectors: `v' = v * M`. Translation is stored in `matrix[3][0..2]` (the bottom row). Matrix concatenation reads left-to-right: `child_to_world = parent_to_world * child_to_parent`.

This convention is enforced consistently across the math library, scene graph, camera matrices, and shaders.

### Coordinate System and Local Basis

Canvas uses a right-handed, Z-up world coordinate system. Each object's local basis is stored in the rows of a 4x4 affine matrix:

| Row | Basis vector | Description                                                             |
|-----|--------------|-------------------------------------------------------------------------|
| 0   | Forward      | Primary facing direction of the object.                                 |
| 1   | Side (left)  | Perpendicular to forward, pointing left.                                |
| 2   | Up           | Perpendicular to forward and side, aligned with world Z when unrotated. |
| 3   | Translation  | World-space position of the object.                                     |

The helper `ComposePointToBasisVectors(worldUp, forward, side, up)` derives an orthonormal side and up pair from a forward direction and a reference up vector. Camera, light, and mesh transforms all follow this layout.

## Dependencies

Canvas pulls in the following libraries as Git submodules or via CMake `FetchContent`:

### Submodules (`deps/`)

| Submodule                                                 | Description                                                |
|-----------------------------------------------------------|------------------------------------------------------------|
| [GEM](https://github.com/wjkristiansen/GEM)               | COM-like interface and smart-pointer framework.            |
| [QLog](https://github.com/wjkristiansen/QLog)             | Lightweight structured logging.                            |
| [ThinWin](https://github.com/wjkristiansen/ThinWin)       | Minimal Win32 window wrapper.                              |
| [InCommand](https://github.com/wjkristiansen/InCommand)   | Command-line argument parsing.                             |
| [WIL](https://github.com/microsoft/wil)                   | Windows Implementation Libraries (error handling helpers). |
| [Allocators](https://github.com/wjkristiansen/Allocators) | Header-only allocators (buddy sub-allocator, etc.).        |
| [ufbx](https://github.com/ufbx/ufbx)                      | Single-file FBX parser.                                    |
| [FbxAssets](https://github.com/wjkristiansen/FbxAssets)   | Sample FBX models for testing.                             |

### Fetched at Configure Time

| Dependency | Description |
|------------|-------------|
| GoogleTest | Unit-test framework. |
| Open-source fonts | Inter and JetBrains Mono, bundled for SDF text rendering. |

## Building

### Current Platform Support

Canvas currently builds and runs on **Windows** with MSVC. The architecture is intended to be cross-platform: CanvasCore and CanvasMath have no platform-specific dependencies, but only the Windows toolchain is tested today. Contributions toward Linux / macOS support are welcome.

### Prerequisites

- Visual Studio 2022 (or the MSVC Build Tools) with the C++ workload
- CMake 3.24 or later
- Windows SDK (10.0.19041.0 or later recommended)

### Configure and Build

```bash
# Clone with submodules
git clone --recursive https://github.com/wjkristiansen/Canvas.git
cd Canvas

# Configure (Visual Studio generator)
cmake -S . -B build

# Build
cmake --build build --config Release

# Install (optional, copies binaries + assets to install/)
cmake --install build --config Release
```

The runtime staging system copies all binaries, shaders, fonts, and SDK DLLs into a single shared directory under `build/` so that executables can find their dependencies without a full install.

## Unit Tests

The `CanvasUnitTest` target uses GoogleTest and covers:

- **CanvasMathTest**: vector, matrix, and quaternion operations
- **CanvasInterfacesTest**: GEM interface map and QueryInterface behaviour
- **CanvasTextTest**: font loading and text layout
- **D3D12ResourceUtilsTest**: graphics-layer resource helpers
- **GpuTaskGraphTest**: barrier resolution and task-graph correctness

```bash
cd build
ctest --build-config Release --output-on-failure
```

## Repository Structure

| Path                     | Description                                                    |
|--------------------------|----------------------------------------------------------------|
| `CMakeLists.txt`         | Root build file                                                |
| `cmake/`                 | CMake modules (WindowsConfig, RuntimeStaging, font/ufbx fetch) |
| `deps/`                  | Git submodules (GEM, QLog, ThinWin, InCommand, WIL, ...)       |
| `docs/`                  | Design documents                                               |
| `scripts/`               | Utility scripts                                                |
| `src/Inc/`               | Public headers (CanvasCore.h, CanvasGfx.h, CanvasMath.hpp)     |
| `src/CanvasCore/`        | Core library implementation                                    |
| `src/CanvasGfx12/`       | Graphics backend                                               |
| `src/CanvasText/`        | Font and SDF glyph generation                                  |
| `src/CanvasFbx/`         | FBX import                                                     |
| `src/RectanglePacker/`   | Rectangle bin-packing                                          |
| `src/HLSL/`              | Shader source and compiled header output                       |
| `src/Common/`            | Shared utilities                                               |
| `src/CanvasModelViewer/` | Windowed viewer application                                    |
| `src/CanvasConsole/`     | Console test harness                                           |
| `src/CanvasUnitTest/`    | GoogleTest unit tests                                          |

## License

This project is licensed under the [MIT License](LICENSE).
