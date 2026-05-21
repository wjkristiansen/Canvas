//================================================================================================
// SkyCubeLoader
//
// Loads a CanvasTerrainViewer skybox cubemap preset from six PNG faces on disk
// into a single GPU cube surface (DimensionCube, ArraySize=1).  Files follow
// the convention
//
//     <assetsDir>/sky_<preset>_{posx,negx,posy,negy,posz,negz}.png
//
// matching the layout produced by scripts/terrain/gen_skycube.py.  All six
// faces must share dimensions and be RGBA8.
//
// Callers are responsible for scheduling the COMMON -> SHADER_RESOURCE
// transition (via XGfxRenderQueue::FinalizeUploadAsShaderResource) before the
// surface is sampled by the composite pass.
//================================================================================================

#pragma once

#include "CanvasCore.h"
#include "CanvasGfx.h"
#include <filesystem>
#include <Gem.hpp>

namespace Canvas
{
namespace TerrainViewer
{

// Load a single sky cubemap preset.  Returns false (and writes nullptr to
// *ppOutCube) on any per-face load failure, dimension mismatch, or GPU upload
// error.  On success *ppOutCube receives an AddRef'd pointer that the caller
// owns (assign into a TGemPtr to manage lifetime).  pLogger is optional.
bool LoadSkyCube(
    XGfxDevice*                  pDevice,
    const std::filesystem::path& assetsDir,
    const char*                  presetName,
    XGfxSurface**                ppOutCube,
    XLogger*                     pLogger = nullptr);

} // namespace TerrainViewer
} // namespace Canvas
