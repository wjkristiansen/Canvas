//================================================================================================
// pch - the precompiled header of CpkgTool: the standard library and the stable libraries cpkg
// uses
//
// cpkg's own headers stay out of it, since they change often, and so do platform headers, which
// FileSystem.cpp and Main.cpp include themselves. cpkg's headers keep their own includes, so each
// compiles without this one.
//================================================================================================

#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cfloat>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <map>
#include <memory>
#include <mutex>
#include <new>
#include <numbers>
#include <optional>
#include <ostream>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include <nlohmann/json.hpp>

#include "CanvasPkg.h"
#include "CanvasProject.h"
#include "InCommand.h"
#include "JsonText.h"
