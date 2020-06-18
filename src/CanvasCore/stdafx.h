// stdafx.h : include file for standard system include files,
// or project specific include files that are used frequently, but
// are changed infrequently
//

#pragma once

#include "targetver.h"

#define WIN32_LEAN_AND_MEAN             // Exclude rarely-used stuff from Windows headers
// Windows Header Files
#include <windows.h>
#include <atlbase.h>
#include <d3d12.h>
#include <intrin.h>

#include <new>
#include <vector>
#include <string>
#include <map>
#include <set>
#include <deque>
#include <unordered_set>
#include <unordered_map>
#include <iostream>
#include <sstream>
#include <mutex>
#include <chrono>

// reference additional headers your program requires here
#include <Gem.hpp>
#include <CanvasMath.hpp>
#include <CanvasCore.h>

#include <SlimLog.h>

using namespace Canvas;
using namespace Gem;

#include "CanvasObject.h"
#include "NamedObject.h"
#include "Transform.h"
#include "SceneGraph.h"
#include "Camera.h"
#include "Light.h"
#include "Model.h"
#include "Scene.h"
#include "CanvasGraphics.h"
#include "GraphicsDevice.h"
#include "Timer.h"
#include "Canvas.h"

#include "CanvasGraphics12.h"
