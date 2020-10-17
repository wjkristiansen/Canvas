// pch.h: This is a precompiled header file.
// Files listed below are compiled only once, improving build performance for future builds.
// This also affects IntelliSense performance, including code completion and many code browsing features.
// However, files listed here are ALL re-compiled if any one of them is updated between builds.
// Do not add files here that you will be updating frequently as this negates the performance advantage.

#ifndef PCH_H
#define PCH_H

// add headers that you want to pre-compile here
#include "framework.h"

#define VK_NO_PROTOTYPES
#define VK_USE_PLATFORM_WIN32_KHR
#include <Windows.h>
#include <vulkan.h>
#include <assert.h>

#include <wil/resource.h>
#include <QLog.h>
#include <Gem.hpp>
#include <atlbase.h>
#include <CanvasMath.hpp>
#include <CanvasCore.h>
#include <CanvasGfx.h>

#include <vector>
#include <deque>
#include <mutex>
#include <memory>

#endif //PCH_H	
