// pch.h : include file for standard system include files,
// or project specific include files that are used frequently, but
// are changed infrequently
//

#pragma once

#include "targetver.h"

// Windows Header Files:
#include <windows.h>
#include <assert.h>


// D3D
#include <d3d12.h>
#include <d3dx12.h>
#include <dxgi1_6.h>
#include <atlbase.h>
#include <comdef.h>
#include <intrin.h>

// STL
#include <string>
#include <memory>
#include <map>
#include <deque>
#include <vector>
#include <iostream>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <functional>
#include <optional>

// Other project headers
#include <Gem.hpp>
#include "CanvasMath.hpp"
#include "CanvasCore.h"
#include "CanvasGfx.h"

inline void ThrowFailedHResult(HRESULT hr)
{
    if (FAILED(hr))
    {
        throw(_com_error(hr));
    }
}

// Forward declarations
namespace Canvas
{
    class CDevice12;
}

// Local headers
#include "CanvasGfx12.h"
#include "Device12.h"
#include "Surface12.h"
#include "SwapChain12.h"
#include "RenderQueue12.h"
