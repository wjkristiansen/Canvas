#pragma once
#include "Gem.hpp"

namespace Canvas::Platform::Win32 {

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
// Platform window — Win32 window lifecycle and FPS mouse capture.
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

    // FPS mouse capture: hides cursor, clips it to the client rect, and registers
    // raw mouse input.
    GEMMETHOD_(void, SetMouseCaptured)(bool captured) = 0;
    GEMMETHOD_(bool, IsMouseCaptured)() = 0;
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
