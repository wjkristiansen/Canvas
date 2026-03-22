#pragma once

// Standard library headers
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>
#include <unordered_map>
#include <memory>
#include <limits>
#include <algorithm>
#include <string>

#if defined(_WIN32) || defined(_WIN64)
    #include <Windows.h>
#endif

// Canvas headers
#include "Gem.hpp"
#include "CanvasCore.h"
#include "CanvasGfx.h"

// Disable common warnings
#ifdef _MSC_VER
    #pragma warning(push)
    #pragma warning(disable: 4324) // Structure alignment warnings
#endif
