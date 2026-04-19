// pch.h : include file for standard system include files,
// or project specific include files that are used frequently, but
// are changed infrequently
//

#pragma once

#include "targetver.h"

// Windows Header Files
#include <windows.h>

#if CANVASFBX_HAS_UFBX
#include <ufbx.h>
#endif

// Canvas headers
#include <Gem.hpp>
#include "CanvasCore.h"
#include "CanvasMath.hpp"

#include <filesystem>
#include <vector>
#include <string>
#include <cstdint>
