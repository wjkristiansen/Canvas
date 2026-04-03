// CanvasFbx.cpp : Scene import entry point
//

#include "pch.h"
#include "CanvasFbx.h"

namespace Canvas
{
namespace Fbx
{

//------------------------------------------------------------------------------------------------
bool ImportedScene::HasErrors() const
{
    for (const auto &d : Diagnostics)
    {
        if (d.Level == DiagLevel::Error)
            return true;
    }
    return false;
}

//------------------------------------------------------------------------------------------------
#if CANVASFBX_HAS_UFBX

// TODO: Phase 2 — ufbx backend implementation
HRESULT ImportScene(
    _In_z_  const wchar_t  *pFilePath,
    _In_    const ImportOptions &options,
    _Out_   ImportedScene  *pScene)
{
    if (!pFilePath || !pScene)
        return E_INVALIDARG;

    (void)options;
    *pScene = {};
    pScene->Diagnostics.push_back({ DiagLevel::Error, "ufbx backend not yet implemented" });
    return E_NOTIMPL;
}

#else // stub when ufbx is not available

HRESULT ImportScene(
    _In_z_  const wchar_t  *pFilePath,
    _In_    const ImportOptions &options,
    _Out_   ImportedScene  *pScene)
{
    if (!pFilePath || !pScene)
        return E_INVALIDARG;

    (void)options;
    *pScene = {};
    pScene->Diagnostics.push_back({ DiagLevel::Error, "CanvasFbx built without ufbx — import unavailable" });
    return E_NOTIMPL;
}

#endif

} // namespace Fbx
} // namespace Canvas
