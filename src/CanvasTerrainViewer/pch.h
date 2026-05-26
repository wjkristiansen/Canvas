// pch.h : precompiled header for CanvasTerrainViewer
#pragma once

// Windows
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <wincodec.h>
#include <atlbase.h>
#include <conio.h>

// CRT
#include <stdlib.h>
#include <malloc.h>
#include <memory.h>
#include <tchar.h>
#include <stdio.h>
#include <intrin.h>

// STL
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

// CanvasPlatformWin32
#include "CanvasPlatformWin32.h"

// GEM
#include "Gem.hpp"

// Canvas
#include "CanvasMath.hpp"
#include "CanvasCore.h"
#include "CanvasGfx.h"

// Logger
#include "QLog.h"
