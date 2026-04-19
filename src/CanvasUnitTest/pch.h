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

// Headers for CppUnitTest
#include "Gem.hpp"
#include "CppUnitTest.h"
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
    using Microsoft::VisualStudio::CppUnitTestFramework::Assert;
    Assert::IsTrue(Succeeded(Canvas::CreateCanvas(nullptr, &pCanvas)));
    Gem::TGemPtr<Canvas::XCanvasPlugin> pPlugin;
    Assert::IsTrue(Succeeded(pCanvas->LoadPlugin("CanvasGfx12.dll", &pPlugin)));
    Assert::IsTrue(Succeeded(pPlugin->CreateCanvasElement(
        pCanvas, Canvas::TypeId::TypeId_GfxDevice, "TestDevice",
        Canvas::XGfxDevice::IId, reinterpret_cast<void**>(&pDevice))));
}

// TODO: reference additional headers your program requires here
