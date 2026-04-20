# DEFENSIVE_DISCLOSURE.md

**Title**: Layered Graphics Engine with Plugin-Based Backend, Interface-Driven Scene Graph, and DAG-Based GPU Barrier Management

**Author**: Bill Kristiansen
**Date of Public Disclosure**: April 20, 2026
**Repository**: [Canvas](https://github.com/wjkristiansen/Canvas)

---

## Purpose

This document serves as a formal defensive publication of the architectural and functional design of the *Canvas* 3D graphics engine. Its intent is to establish prior art and prevent future patent claims on the core concepts and mechanisms described herein.

---

## Abstract

Canvas implements a layered graphics engine architecture that cleanly separates API-agnostic scene management from GPU backend implementation through a COM-style interface model (GEM). The engine explores patterns for efficient real-time rendering including DAG-based automatic barrier management, multi-timeline GPU resource lifecycle, and a plugin-based backend architecture that enables graphics-API independence at the core layer.

---

## Key Innovations

### 1. Interface-Driven Core/Backend Separation

- All public Canvas types are expressed as GEM interfaces with reference counting and QueryInterface.
- CanvasCore defines scene graph, camera, light, mesh, UI, and font interfaces with no graphics-API dependency.
- Graphics backends are loaded as DLL plugins at runtime via a `CreateCanvasPlugin` factory export.
- This enables binary-stable ABI boundaries and allows alternative backend implementations without modifying core code.

### 2. GPU Task Graph with Declarative Barrier Resolution

- GPU work is organized as a directed acyclic graph (DAG) of tasks.
- Each task declares its resource usage (layout, sync, access) rather than manually inserting barriers.
- The graph automatically resolves D3D12 Enhanced Barriers based on declared transitions between tasks.
- Cross-ECL layout fixups are bridged via `GetInitialLayouts()` / `GetFinalLayouts()` on the graph.

### 3. Multi-Timeline Deferred Resource Lifecycle

- A device-level `CResourceManager` tracks GPU resource lifetimes across multiple command queues.
- Each queue registers a fence and receives a stable `TimelineId`; deferred operations carry a `FenceToken {TimelineId, FenceValue}`.
- Power-of-2 bucketed buffer pools enable allocation reuse; retired buffers are reclaimed per-timeline when their fence completes.
- This design avoids single-queue bottlenecks and correctly handles cross-queue resource sharing.

### 4. Hierarchical Scene Graph with Lazy Transform Propagation

- Scene graph nodes store local rotation (quaternion), translation, and scale independently.
- Global transforms are computed lazily via dirty-flag propagation, avoiding redundant matrix recomputation.
- The row-vector matrix convention (v' = v * M) is enforced consistently from the math library through shaders.

### 5. UI Graph with Dirty-Tracked Update and Device-Managed Vertex Buffers

- A 2-D UI overlay graph provides hierarchical positioning with dirty-tracked update propagation.
- UI element vertex buffers are allocated and uploaded by the graphics device, keeping GPU resource management out of the core layer.
- SDF-based text rendering and filled-rectangle primitives compose via separate draw command batches.
