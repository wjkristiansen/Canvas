#pragma once
#include "Gem.hpp"
#include "CanvasFormat.h"
#include <cstdint>

namespace Canvas { struct XLogger; }

namespace Canvas::Platform::Win32 {

//------------------------------------------------------------------------------------------------
// XImage - a decoded image returned by LoadImageData. The pixel storage lives entirely inside
// the implementation, so no STL container crosses the DLL boundary. Pixels are row-major; the
// row stride is GetWidth() * GetBytesPerPixel() bytes.
//------------------------------------------------------------------------------------------------
struct XImage : public Gem::XGeneric
{
    GEM_INTERFACE_DECLARE(XImage, 0x7A1C39E6D2840FB5);

    GEMMETHOD_(uint32_t, GetWidth)() = 0;
    GEMMETHOD_(uint32_t, GetHeight)() = 0;
    GEMMETHOD_(Canvas::GfxFormat, GetFormat)() = 0;
    GEMMETHOD_(uint32_t, GetBytesPerPixel)() = 0;

    // Pointer to the row-major pixel bytes, valid for the lifetime of this object.
    // Returns nullptr when the image is empty.
    GEMMETHOD_(const uint8_t*, GetPixels)() = 0;

    // Total size of the pixel buffer in bytes (Height * GetWidth() * GetBytesPerPixel()).
    GEMMETHOD_(size_t, GetPixelByteCount)() = 0;

    // True when no pixels are present (zero dimensions or an empty buffer).
    GEMMETHOD_(bool, IsEmpty)() = 0;
};

//------------------------------------------------------------------------------------------------
// Image loading - WIC-backed loader. Accepts any Canvas::GfxFormat that has a direct WIC
// IWICFormatConverter mapping; unsupported formats fail.
// Currently supported: R8G8B8A8_UNorm, R16G16B16A16_Float, R8_UNorm, R16_UNorm.
//
// On success *ppImage receives a new XImage (the caller owns the reference); on failure
// *ppImage is set to null and an error is logged via pLogger (if non-null).
//------------------------------------------------------------------------------------------------
Gem::Result LoadImageData(
    const wchar_t*     path,
    Canvas::GfxFormat  format,
    XImage**           ppImage,
    Canvas::XLogger*   pLogger = nullptr);

// Memory-based overload: decode from raw image bytes (e.g. an embedded FBX texture).
Gem::Result LoadImageData(
    const uint8_t*     data,
    size_t             byteCount,
    Canvas::GfxFormat  format,
    XImage**           ppImage,
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
// Raw input interface - keyboard, mouse delta, buttons, and scroll.
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

    // Keyboard - level query (held) and edge queries (valid for one frame after Update).
    GEMMETHOD_(bool, IsKeyDown)(int vk) = 0;
    GEMMETHOD_(bool, IsKeyPressed)(int vk) = 0;   // went down this frame
    GEMMETHOD_(bool, IsKeyReleased)(int vk) = 0;  // went up this frame

    // Accumulated mouse delta since the last Update call.
    GEMMETHOD_(void, GetMouseDelta)(float& dx, float& dy) = 0;

    // Mouse buttons (0=left, 1=right, 2=middle, 3=X1, 4=X2).
    GEMMETHOD_(bool, IsMouseButtonDown)(int button) = 0;
    GEMMETHOD_(bool, IsMouseButtonPressed)(int button) = 0;
    GEMMETHOD_(bool, IsMouseButtonReleased)(int button) = 0;

    // Scroll wheel - returns accumulated ticks and zeroes the counter.
    GEMMETHOD_(int, ConsumeScrollDelta)() = 0;
};

//------------------------------------------------------------------------------------------------
// Platform window - Win32 window lifecycle and exclusive (relative) mouse capture.
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
    // painting tools, etc.) - the mechanism itself is policy-free.
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
// CreatePlatformWindow - atomically creates a window and a bound raw-input object.
// The impl classes are privately wired; neither public interface exposes the other.
// Both objects must remain alive simultaneously.
//
// CreateAppWindow - creates a window with no raw-input binding (e.g. UI-only tools).
//------------------------------------------------------------------------------------------------
Gem::Result CreatePlatformWindow(const AppWindowDesc& desc, XAppWindow** ppWindow, XRawInput** ppInput);
Gem::Result CreateAppWindow(const AppWindowDesc& desc, XAppWindow** ppWindow);

} // namespace Canvas::Platform::Win32

