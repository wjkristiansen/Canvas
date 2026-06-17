# CanvasPlatformWin32

The Win32 platform layer for Canvas. CanvasPlatformWin32 is a static library that wraps the Win32 window lifecycle, message pump, exclusive mouse capture, raw keyboard / mouse / scroll input, and WIC-based image decoding, so the rest of the Canvas codebase never needs to include `<windows.h>` or talk to `HWND`s, `IWICImagingFactory`s, etc. directly.

This document describes the current state of the module. CanvasPlatformWin32 is still being developed; the surface described here may be extended as new platform needs arise.

## Overview

CanvasPlatformWin32 publishes two interfaces from `CanvasPlatformWin32.h` in the `Canvas::Platform::Win32` namespace:

- **`XAppWindow`** - owns a top-level Win32 window. Provides `Show`, `GetHWND`, `HasFocus`, `PumpMessages`, `SetMouseCaptured` / `IsMouseCaptured` for exclusive mouse capture, and `SetWindowSize` / `GetClientSize` / `SetFullscreen` / `IsFullscreen` for window sizing and borderless-fullscreen control (see [Window Sizing & Fullscreen](#window-sizing--fullscreen)).
- **`XRawInput`** - accumulates keyboard, mouse-delta, mouse-button, and scroll-wheel state from `WM_INPUT` messages. Exposes both level queries (`IsKeyDown`, `IsMouseButtonDown`) and one-frame edge queries (`IsKeyPressed`, `IsKeyReleased`, `IsMouseButtonPressed`, `IsMouseButtonReleased`), plus `GetMouseDelta` and `ConsumeScrollDelta`.

It also exposes a free-function image-loading API in the same namespace:

- **`LoadImageData`** - decodes an image from a file path or in-memory buffer into a tightly-packed `XImage` of a caller-chosen `Canvas::GfxFormat`. Implemented on top of the Windows Imaging Component (WIC).

Both GEM interfaces derive from `Gem::XGeneric`. GEM is the lightweight COM-style object model used throughout Canvas; it provides reference counting (`AddRef`/`Release`), interface discovery (`QueryInterface`), and 64-bit interface IDs. Smart pointers (`Gem::TGemPtr<T>`) automate lifetime management. See the [GEM repository](../../deps/GEM) for details.

The window implementation is built on top of [ThinWin](../../deps/ThinWin), a thin C++ wrapper around the Win32 window class / `WindowProc` boilerplate. The image-loading implementation uses WIC (`windowscodecs.dll`) and ATL `CComPtr` for COM lifetime management.

## Factories

Two creation entry points are exported:

```cpp
Gem::Result CreatePlatformWindow(const AppWindowDesc& desc,
                                 XAppWindow**          ppWindow,
                                 XRawInput**           ppInput);

Gem::Result CreateAppWindow(const AppWindowDesc& desc,
                            XAppWindow**          ppWindow);
```

- **`CreatePlatformWindow`** atomically creates a window and a raw-input object that are privately wired to each other at the implementation level. Neither public interface exposes a pointer to the other, but the window's `WindowProc` forwards `WM_INPUT` into the raw-input object, and `PumpMessages` drives `XRawInput::Update` once per frame. Both objects must remain alive simultaneously; releasing one while the other is still in use is a bug.
- **`CreateAppWindow`** creates a window with no raw-input binding - appropriate for UI-only tools that do not need keyboard / mouse polling and prefer to handle window messages themselves.

`AppWindowDesc` carries the basic window parameters:

| Field        | Default              | Notes                                                              |
|--------------|----------------------|--------------------------------------------------------------------|
| `Title`      | `"Canvas"`           | Window caption.                                                    |
| `hInstance`  | `nullptr`            | Falls back to `GetModuleHandleA(nullptr)` when null.               |
| `Width`      | `1280`               | Initial client-area width (passed through to `CreateWindowEx`).    |
| `Height`     | `720`                | Initial client-area height.                                        |
| `Style`      | `WS_OVERLAPPEDWINDOW`| Standard Win32 window style.                                       |
| `hIcon`      | `nullptr`            | Large icon registered with the window class.                       |
| `hIconSm`    | `nullptr`            | Small icon registered with the window class.                       |

## Message Loop

`XAppWindow::PumpMessages` is the per-frame entry point:

1. If a raw-input object is bound, call `XRawInput::Update` - this latches the previous frame's key/button state for edge detection, zeros the mouse-delta accumulator, and samples whether a Remote Desktop session is active.
2. Drain the Win32 message queue with `PeekMessageW` / `TranslateMessage` / `DispatchMessage`. `WM_INPUT` is forwarded into the raw-input object by the window's `WindowProc`.
3. Return `false` on `WM_QUIT`, otherwise `true`.

No per-frame cursor warp is performed. Local devices send position-independent `MOUSE_MOVE_RELATIVE` deltas, so `ClipCursor` alone contains the hidden cursor; under Remote Desktop the cursor is recentred on demand inside `ProcessRawInput` (see [Remote Desktop](#remote-desktop)).

The intent is that callers drive the application from a single `while (pWindow->PumpMessages()) { /* simulate + render */ }` loop without writing any Win32 plumbing themselves.

## Exclusive Mouse Capture

`SetMouseCaptured(true)` enables an exclusive (relative) capture mode suitable for any interaction that wants unbounded mouse motion without the cursor visibly leaving the window - first-person cameras, orbit / pan cameras, modal drag operations, painting tools, etc. The mechanism itself is policy-free; what the deltas mean is up to the caller.

- `SetCapture` routes mouse messages to the window even when the cursor leaves it.
- The cursor is hidden and clipped to the client rect with `ClipCursor`.
- A `RAWINPUTDEVICE` for the generic mouse (usage page 0x01, usage 0x02) is registered against the window's `HWND`, so high-resolution movement deltas arrive via `WM_INPUT`.

`SetMouseCaptured(false)` reverses each step: unregisters the raw mouse device, releases `ClipCursor` / `ReleaseCapture`, and restores the arrow cursor. Capture is also released automatically on `WM_CAPTURECHANGED` (when another window steals capture) and on `WM_ACTIVATEAPP` with `wParam == FALSE` (when the app loses focus). The latter additionally calls `XRawInput::ClearState` so no key or button appears stuck-down when focus returns.

The keyboard raw-input device (usage page 0x01, usage 0x06) is registered unconditionally during `Initialize` so that `IsKeyDown` works from the very first frame, independent of capture state.

## Window Sizing & Fullscreen

`XAppWindow` exposes four methods for runtime window sizing and a borderless-fullscreen toggle:

```cpp
void SetWindowSize(int width, int height);   // resize the client area (windowed mode)
void GetClientSize(int& width, int& height); // current client-area size in pixels
void SetFullscreen(bool fullscreen);         // enter / leave borderless fullscreen
bool IsFullscreen();                         // current mode
```

- **`SetWindowSize`** sizes the *client area* to the requested dimensions. The required outer window rect is computed with `AdjustWindowRectEx` against the window's current style, so the drawable area matches the request regardless of border / caption thickness. It is a no-op while fullscreen.
- **`GetClientSize`** returns the live `GetClientRect` dimensions. A caller polls this each frame to detect user-driven resizes (border drag, maximize) and to drive swap-chain back-buffer resizing.
- **`SetFullscreen(true)`** switches to **borderless fullscreen** (not exclusive DXGI fullscreen): it saves the current window style, extended style, and `WINDOWPLACEMENT`, drops the `WS_OVERLAPPEDWINDOW` decorations for `WS_POPUP`, and resizes the window to cover the full monitor rect. The monitor chosen is the one returned by `MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST)` - i.e. the display that most-contains the window - so the window goes fullscreen on whichever monitor it currently occupies.
- **`SetFullscreen(false)`** restores the saved style / extended style / placement, returning the window to its previous size and position.

When the mouse is captured, the cursor clip rect is recomputed after any size or fullscreen change so the hidden cursor stays clipped to the new client bounds.

Deciding *what* an Alt-key chord does is the responsibility of the consuming application, not the platform layer - Alt combinations are reported through the normal raw-input state (the Alt key as `VK_MENU`), so a caller detects a chord such as Alt+Enter the same way it reads any other key. The platform layer's only involvement is suppressing the default system beep: because the window has no menu bar, an unhandled Alt chord would otherwise make `DefWindowProc` ding, so the window swallows `WM_SYSCHAR`.

Changing the window size - whether via `SetWindowSize`, entering or leaving fullscreen, or a user-driven border drag / maximize - updates the window and its client area but does **not** itself resize the swap-chain back buffers; that remains the consuming application's responsibility. `GetClientSize` exposes the current client dimensions so a caller can detect the change (typically by polling each frame) and resize its swap chain to match, keeping the rendered resolution equal to the client/monitor resolution.

## Raw Input Semantics

`XRawInput` keeps two parallel state arrays for keys and buttons - current and previous - so that edge queries are exact for one frame:

| Query                       | Meaning                                                          |
|-----------------------------|------------------------------------------------------------------|
| `IsKeyDown(vk)`             | Key is currently held.                                           |
| `IsKeyPressed(vk)`          | Key transitioned up -> down since the previous `Update`.         |
| `IsKeyReleased(vk)`         | Key transitioned down -> up since the previous `Update`.         |
| `IsMouseButtonDown(b)`      | Button is currently held (`b` is 0=L, 1=R, 2=M, 3=X1, 4=X2).     |
| `IsMouseButtonPressed(b)`   | Button transitioned up -> down since the previous `Update`.      |
| `IsMouseButtonReleased(b)`  | Button transitioned down -> up since the previous `Update`.      |
| `GetMouseDelta(dx, dy)`     | Accumulated mouse motion in pixels since the previous `Update`.  |
| `ConsumeScrollDelta()`      | Accumulated wheel ticks; reads and zeroes in one call.           |

Mouse motion accepts both `MOUSE_MOVE_RELATIVE` and `MOUSE_MOVE_ABSOLUTE` raw packets. Local pointing devices send `MOUSE_MOVE_RELATIVE` deltas, which come straight from the device independent of cursor position and are accumulated directly - no cursor warp is required, so capture relies on `ClipCursor` alone to contain the hidden cursor. Local absolute devices (tablets/digitizers) send `MOUSE_MOVE_ABSOLUTE` samples that are converted to virtual-desktop or primary-screen pixels (per the `MOUSE_VIRTUAL_DESKTOP` flag) and differenced against the previous sample, with the baseline reset each `Update`.

### Remote Desktop

Over Remote Desktop (RDP) the mouse is delivered as a stream of *absolute* virtual-desktop positions rather than relative deltas, and two RDP-specific OS behaviours break a naive centre-warp:

1. **A hidden cursor stops `SetCursorPos` from working.** Hiding the captured cursor with `SetCursor(NULL)` causes the per-frame recentre warp to silently no-op under RDP, so the client cursor drifts to the virtual-desktop edge where absolute packets clamp and mouse-look freezes. The implementation therefore installs a fully **transparent cursor** (`CreateCursor` with an all-ones AND mask) while captured instead of removing the cursor.
2. **Identical `SetCursorPos` targets are coalesced and dropped.** Warping to the exact same centre every frame is ignored by RDP. The recentre (`CRawInput::WarpToWindowCentre`) therefore **jitters** the target by a pixel across frames and issues the call repeatedly (`x`, `x+1`, `x`) to defeat the coalescing.

Detection is via `GetSystemMetrics(SM_REMOTESESSION)`, sampled each `Update` so it tracks live connect/disconnect transitions. When a remote session is active, motion handling follows the same approach SDL uses for relative mouse over RDP:

- A **persistent baseline** (not reset per frame) is differenced against each absolute sample to produce relative motion.
- When the cursor nears any screen edge (within 1%, or `y < 32` to catch the RDP title strip) it is **recentred instead of emitting motion**.
- The large jump produced by a recentre is rejected via a `screenHeight / 6` motion clamp, so the warp never leaks into the camera.
- The local path performs no warp at all; under RDP the recentre is edge-triggered inside `ProcessRawInput`.

This keeps mouse-look sensitivity identical to the local case while remaining usable over a multi-monitor RDP session. Frame rate over RDP is still poor; for latency-sensitive work a game-streaming protocol (Parsec, Sunshine/Moonlight, Steam Remote Play) remains preferable, but mouse-look is no longer the limiting factor.

`Update` is called for you by `XAppWindow::PumpMessages` when both objects were created via `CreatePlatformWindow`. Call it yourself only when driving a custom message loop. `ClearState` exists for callers that need to drop all held-key / held-button state explicitly - for example, after a modal dialog or some other focus excursion that bypasses the normal `WM_ACTIVATEAPP` path.

## Image Loading

`LoadImageData` decodes an image into a CPU-side pixel buffer suitable for handing to `XGfxDevice::UploadTextureRegion` or any other consumer that wants raw pixels. The decoded image is returned as an `XImage` GEM object; the pixel storage lives entirely inside the implementation, so no STL container crosses the DLL boundary. Two overloads are provided:

```cpp
struct XImage : public Gem::XGeneric
{
    GEMMETHOD_(uint32_t, GetWidth)() = 0;
    GEMMETHOD_(uint32_t, GetHeight)() = 0;
    GEMMETHOD_(Canvas::GfxFormat, GetFormat)() = 0;
    GEMMETHOD_(uint32_t, GetBytesPerPixel)() = 0;
    GEMMETHOD_(const uint8_t*, GetPixels)() = 0;       // tightly packed, row pitch = Width * GetBytesPerPixel()
    GEMMETHOD_(size_t, GetPixelByteCount)() = 0;
    GEMMETHOD_(bool, IsEmpty)() = 0;
};

Gem::Result LoadImageData(const wchar_t*    path,
                          Canvas::GfxFormat format,
                          XImage**          ppImage,
                          Canvas::XLogger*  pLogger);

Gem::Result LoadImageData(const uint8_t*    data,
                          size_t            byteCount,
                          Canvas::GfxFormat format,
                          XImage**          ppImage,
                          Canvas::XLogger*  pLogger);
```

The path overload opens the file directly via `IWICImagingFactory::CreateDecoderFromFilename`; the memory overload copies the bytes into an `HGLOBAL`-backed `IStream` and decodes via `CreateDecoderFromStream`. Both paths funnel through the same WIC `IWICFormatConverter` step, so the output is always tightly packed in the requested `GfxFormat` regardless of the source pixel layout.

Supported target formats:

| `GfxFormat`            | WIC pixel format                    | Bytes / pixel |
|------------------------|-------------------------------------|---------------|
| `R8G8B8A8_UNorm`       | `GUID_WICPixelFormat32bppRGBA`      | 4             |
| `R16G16B16A16_Float`   | `GUID_WICPixelFormat64bppRGBAHalf`  | 8             |
| `R8_UNorm`             | `GUID_WICPixelFormat8bppGray`       | 1             |
| `R16_UNorm`            | `GUID_WICPixelFormat16bppGray`      | 2             |

Any other `GfxFormat` value is rejected with an `XLogger` error and a failing `Gem::Result`; if you need a new format, extend the switch in `ImageLoader.cpp`.

Both overloads return a failing `Gem::Result` on any failure (null/empty input, unsupported format, file-open error, decoder error, format-converter error, `CopyPixels` error) and leave `*ppImage` null. On failure they log an error through the supplied `XLogger`; on success they log an info line with the decoded dimensions and source. A null `XLogger` is allowed - log calls are routed through the standard `LogError` / `LogInfo` helpers, which no-op when the logger is null.

**COM apartment.** WIC requires COM to be initialized on the calling thread. CanvasPlatformWin32 does **not** call `CoInitializeEx` for you; consumers that decode images off the main thread are responsible for initializing COM on those threads themselves.

## Build

CanvasPlatformWin32 is a CMake static library target built as part of the top-level Canvas build:

- Sources: `AppWindow.{h,cpp}`, `RawInput.{h,cpp}`, `ImageLoader.cpp`, `pch.h`, plus the public header `../Inc/CanvasPlatformWin32.h`.
- C++ standard: C++17.
- Compile definitions: `NOMINMAX`, `_WINDOWS`.
- Public dependencies: `GEM`, `windowscodecs` (the WIC import library - public so consumers that statically link this target also get WIC).
- Private dependencies: `ThinWin`, `user32`, `kernel32`.
- Uses `pch.h` as a precompiled header (pulls in `<windows.h>`, `<wincodec.h>`, `<atlbase.h>`, `<ThinWin.h>`, and the public header).

The static library is installed to `${CANVAS_INSTALL_DEST}/lib` and the public header to `include/`.

## Usage

```cpp
#include "CanvasPlatformWin32.h"

using namespace Canvas::Platform::Win32;

AppWindowDesc desc;
desc.Title  = "My Canvas App";
desc.Width  = 1920;
desc.Height = 1080;

Gem::TGemPtr<XAppWindow> pWindow;
Gem::TGemPtr<XRawInput>  pInput;
if (Gem::Failed(CreatePlatformWindow(desc, &pWindow, &pInput)))
    return -1;

pWindow->Show(SW_SHOW);
pWindow->SetMouseCaptured(true);

while (pWindow->PumpMessages())
{
    float dx, dy;
    pInput->GetMouseDelta(dx, dy);

    if (pInput->IsKeyPressed(VK_ESCAPE))
        pWindow->SetMouseCaptured(false);

    // ... simulate, render, present ...
}

// Decode a texture from disk into a tightly-packed RGBA8 pixel block.
Gem::TGemPtr<XImage> img;
if (Gem::Succeeded(LoadImageData(L"assets/diffuse.png",
                  Canvas::GfxFormat::R8G8B8A8_UNorm, &img, pLogger)))
{
    // img->GetWidth() / img->GetHeight() / img->GetPixels() ready to hand to
    // XGfxDevice::UploadTextureRegion.
}
```

Consumers in this repository include [`CanvasModelViewer`](../CanvasModelViewer) and [`CanvasTerrainViewer`](../CanvasTerrainViewer).

