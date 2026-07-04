//================================================================================================
// SerializeScene - convert an imported FBX scene into a PackageData ready for WritePackage.
//
// Maps every Fbx::ImportedScene field to its Canvas::PackageData counterpart and remaps each
// material's texture indices (which reference ImportedScene::Textures) to the deduplicated Texture
// table indices. Texture Format/extents are left at their defaults: v1 does not pre-decode pixel
// data during bake. Internal to CanvasPackage; not part of the public Inc/ surface.
//================================================================================================
#pragma once

#include "CanvasFbx.h"         // Canvas::Fbx::ImportedScene
#include "CanvasPackageData.h" // Canvas::PackageData
#include "TextureTable.h"

namespace Canvas::Cpkg
{

// Populate *out from scene, resolving material texture indices through table (by each referenced
// texture's absolute path). *out is fully replaced.
void SerializeScene(const Canvas::Fbx::ImportedScene& scene, const TextureTable& table,
                    PackageData* out);

} // namespace Canvas::Cpkg
