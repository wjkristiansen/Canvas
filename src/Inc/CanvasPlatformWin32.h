#pragma once
#include "Gem.hpp"
#include "CanvasFormat.h"
#include <cstdint>
#include <vector>

namespace Canvas { struct XLogger; }

namespace Canvas::Platform::Win32 {

//------------------------------------------------------------------------------------------------
// Image loading — WIC-backed loader. Accepts any Canvas::GfxFormat that has a
// direct WIC IWICFormatConverter mapping; unsupported formats return false.
// Currently supported: R8G8B8A8_UNorm, R16G16B16A16_Float, R8_UNorm, R16_UNorm.
//------------------------------------------------------------------------------------------------
struct ImageData
{
    uint32_t              Width  = 0;
    uint32_t              Height = 0;
    Canvas::GfxFormat     Format = Canvas::GfxFormat::Unknown;
    std::vector<uint8_t>  Pixels; // Row-major raw bytes; stride = Width * BytesPerPixel()

    uint32_t BytesPerPixel() const
    {
        switch (Format)
        {
        case Canvas::GfxFormat::R8G8B8A8_UNorm:        return 4;
        case Canvas::GfxFormat::R16G16B16A16_Float:    return 8;
        case Canvas::GfxFormat::R8_UNorm:              return 1;
        case Canvas::GfxFormat::R16_UNorm:             return 2;
        default:                                        return 0;
        }
    }

    bool IsEmpty() const { return Pixels.empty() || Width == 0 || Height == 0; }
};

// Load any WIC-decodable image file and convert to the requested pixel format.
// Returns true on success; on failure outImage is left empty and an error is
// logged via pLogger (if non-null).
bool LoadImageData(
    const wchar_t*     path,
    Canvas::GfxFormat  format,
    ImageData*         outImage,
    Canvas::XLogger*   pLogger = nullptr);

// Memory-based overload: decode from raw image bytes (e.g. an embedded FBX texture).
bool LoadImageData(
    const uint8_t*     data,
    size_t             byteCount,
    Canvas::GfxFormat  format,
    ImageData*         outImage,
    Canvas::XLogger*   pLogger = nullptr);

struct AppWindowDesc
{
    PCSTR     Title     = "Canvas";
    HINSTANCE hInstance = nullptr;
    int       Width     = 1280;
    int       Height    = 720;
    DWORD     Style     = WS_OVERLAPPEDWINDOW;
    HICON     hIcon     = nullptr;
    HICON     hIconSm   = nullptr;
};

//------------------------------------------------------------------------------------------------
// Raw input interface — keyboard, mouse delta, buttons, and scroll.
// Neither interface holds a public reference to the other; the impl-level wiring
// is established atomically by CreatePlatformWindow.
//------------------------------------------------------------------------------------------------
struct XRawInput : public Gem::XGeneric
{
    GEM_INTERFACE_DECLARE(XRawInput, 0xC1F89D403B5E47A2);

    // Advances edge states and zeros the mouse delta accumulator.
    // Called automatically by XAppWindow::PumpMessages; only needed directly
    // when driving a custom message loop without XAppWindow.
    GEMMETHOD_(void, Update)() = 0;

    // Clears all key and button state (e.g. after an unexpected focus loss).
    GEMMETHOD_(void, ClearState)() = 0;

    // Keyboard — level query (held) and edge queries (valid for one frame after Update).
    GEMMETHOD_(bool, IsKeyDown)(int vk) = 0;
    GEMMETHOD_(bool, IsKeyPressed)(int vk) = 0;   // went down this frame
    GEMMETHOD_(bool, IsKeyReleased)(int vk) = 0;  // went up this frame

    // Accumulated mouse delta since the last Update call.
    GEMMETHOD_(void, GetMouseDelta)(float& dx, float& dy) = 0;

    // Mouse buttons (0=left, 1=right, 2=middle, 3=X1, 4=X2).
    GEMMETHOD_(bool, IsMouseButtonDown)(int button) = 0;
    GEMMETHOD_(bool, IsMouseButtonPressed)(int button) = 0;
    GEMMETHOD_(bool, IsMouseButtonReleased)(int button) = 0;

    // Scroll wheel — returns accumulated ticks and zeroes the counter.
    GEMMETHOD_(int, ConsumeScrollDelta)() = 0;
};

//------------------------------------------------------------------------------------------------
// Platform window — Win32 window lifecycle and exclusive (relative) mouse capture.
//------------------------------------------------------------------------------------------------
struct XAppWindow : public Gem::XGeneric
{
    GEM_INTERFACE_DECLARE(XAppWindow, 0x3B5E47A2C1F89D40);

    GEMMETHOD_(void, Show)(int nCmdShow) = 0;
    GEMMETHOD_(HWND, GetHWND)() = 0;

    // Returns true when this window is the foreground window.
    GEMMETHOD_(bool, HasFocus)() = 0;

    // Drains the Win32 message queue.  When a raw-input object was bound at
    // creation time, also calls XRawInput::Update and forwards WM_INPUT, then
    // warps the cursor to the window centre when captured (prevents screen-edge
    // binding in MOUSE_MOVE_ABSOLUTE mode).
    // Returns false when WM_QUIT is received.
    GEMMETHOD_(bool, PumpMessages)() = 0;

    // Exclusive (relative) mouse capture: hides the cursor, clips it to the client
    // rect, and registers raw mouse input.  Suitable for any interaction that wants
    // unbounded mouse motion (first-person cameras, orbit/pan cameras, modal drag,
    // painting tools, etc.) — the mechanism itself is policy-free.
    GEMMETHOD_(void, SetMouseCaptured)(bool captured) = 0;
    GEMMETHOD_(bool, IsMouseCaptured)() = 0;

    // Resizes the window so its client area matches the requested dimensions.
    // No-op while the window is in fullscreen mode.
    GEMMETHOD_(void, SetWindowSize)(int width, int height) = 0;

    // Returns the current client-area size in pixels.
    GEMMETHOD_(void, GetClientSize)(int& width, int& height) = 0;

    // Toggles borderless-fullscreen mode.  Entering fullscreen saves the current
    // window style and placement and resizes the window to cover its monitor;
    // exiting restores them.  Alt+Enter performs the same toggle automatically.
    GEMMETHOD_(void, SetFullscreen)(bool fullscreen) = 0;
    GEMMETHOD_(bool, IsFullscreen)() = 0;
};

//------------------------------------------------------------------------------------------------
// CreatePlatformWindow — atomically creates a window and a bound raw-input object.
// The impl classes are privately wired; neither public interface exposes the other.
// Both objects must remain alive simultaneously.
//
// CreateAppWindow — creates a window with no raw-input binding (e.g. UI-only tools).
//------------------------------------------------------------------------------------------------
Gem::Result CreatePlatformWindow(const AppWindowDesc& desc, XAppWindow** ppWindow, XRawInput** ppInput);
Gem::Result CreateAppWindow(const AppWindowDesc& desc, XAppWindow** ppWindow);

} // namespace Canvas::Platform::Win32
