// pch.h : include file for standard system include files,
// or project specific include files that are used frequently, but
// are changed infrequently
//

#pragma once

#include "targetver.h"

#include <windows.h>
#include <d3d12.h>
#include <d3dx12/d3dx12.h>
#include <atlbase.h>
#include <comdef.h>

// GoogleTest framework
#include <gtest/gtest.h>

#include "Gem.hpp"
#include "CanvasMath.hpp"
#include "CanvasCore.h"
#include "CanvasGfx.h"

using namespace Canvas;
using namespace Gem;

#include <new>
#include <vector>
#include <map>
#include <string>

// Shared helper: spin up a real GFX device for tests that need to construct
// scene-graphs or other objects whose APIs require an XGfxDevice.  Tests run
// from the runtime image directory so CanvasGfx12.dll is loadable.
inline void CreateTestCanvasAndDevice(
    Gem::TGemPtr<Canvas::XCanvas>& pCanvas,
    Gem::TGemPtr<Canvas::XGfxDevice>& pDevice)
{
    ASSERT_TRUE(Succeeded(Canvas::CreateCanvas(nullptr, &pCanvas)));
    Gem::TGemPtr<Canvas::XCanvasPlugin> pPlugin;
    ASSERT_TRUE(Succeeded(pCanvas->LoadPlugin("CanvasGfx12.dll", &pPlugin)));
    ASSERT_TRUE(Succeeded(pPlugin->CreateCanvasElement(
        pCanvas, Canvas::TypeId::TypeId_GfxDevice, "TestDevice",
        Canvas::XGfxDevice::IId, reinterpret_cast<void**>(&pDevice))));
}
