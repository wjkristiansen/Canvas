# CanvasPlatformWin32

The Win32 platform layer for Canvas. CanvasPlatformWin32 is a static library that wraps the Win32 window lifecycle, message pump, exclusive mouse capture, raw keyboard / mouse / scroll input, and WIC-based image decoding, so the rest of the Canvas codebase never needs to include `<windows.h>` or talk to `HWND`s, `IWICImagingFactory`s, etc. directly.

This document describes the current state of the module. CanvasPlatformWin32 is still being developed; the surface described here may be extended as new platform needs arise.

## Overview

CanvasPlatformWin32 publishes two interfaces from `CanvasPlatformWin32.h` in the `Canvas::Platform::Win32` namespace:

- **`XAppWindow`** — owns a top-level Win32 window. Provides `Show`, `GetHWND`, `HasFocus`, `PumpMessages`, and `SetMouseCaptured` / `IsMouseCaptured` for exclusive mouse capture.
- **`XRawInput`** — accumulates keyboard, mouse-delta, mouse-button, and scroll-wheel state from `WM_INPUT` messages. Exposes both level queries (`IsKeyDown`, `IsMouseButtonDown`) and one-frame edge queries (`IsKeyPressed`, `IsKeyReleased`, `IsMouseButtonPressed`, `IsMouseButtonReleased`), plus `GetMouseDelta` and `ConsumeScrollDelta`.

It also exposes a free-function image-loading API in the same namespace:

- **`LoadImageData`** — decodes an image from a file path or in-memory buffer into a tightly-packed `ImageData` pixel block of a caller-chosen `Canvas::GfxFormat`. Implemented on top of the Windows Imaging Component (WIC).

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
- **`CreateAppWindow`** creates a window with no raw-input binding — appropriate for UI-only tools that do not need keyboard / mouse polling and prefer to handle window messages themselves.

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

1. If a raw-input object is bound, call `XRawInput::Update` — this latches the previous frame's key/button state for edge detection and zeros the mouse-delta accumulator.
2. Drain the Win32 message queue with `PeekMessageW` / `TranslateMessage` / `DispatchMessage`. `WM_INPUT` is forwarded into the raw-input object by the window's `WindowProc`.
3. If the mouse is currently captured, warp the cursor back to the centre of the client rect. This prevents `MOUSE_MOVE_ABSOLUTE`-style raw input (touchpads, remote-desktop sessions, some virtual mice) from clamping at the screen edge; the absolute baseline is reset every `Update` so the next sample produces a small delta from centre rather than a spurious jump.
4. Return `false` on `WM_QUIT`, otherwise `true`.

The intent is that callers drive the application from a single `while (pWindow->PumpMessages()) { /* simulate + render */ }` loop without writing any Win32 plumbing themselves.

## Exclusive Mouse Capture

`SetMouseCaptured(true)` enables an exclusive (relative) capture mode suitable for any interaction that wants unbounded mouse motion without the cursor visibly leaving the window — first-person cameras, orbit / pan cameras, modal drag operations, painting tools, etc. The mechanism itself is policy-free; what the deltas mean is up to the caller.

- `SetCapture` routes mouse messages to the window even when the cursor leaves it.
- The cursor is hidden and clipped to the client rect with `ClipCursor`.
- A `RAWINPUTDEVICE` for the generic mouse (usage page 0x01, usage 0x02) is registered against the window's `HWND`, so high-resolution movement deltas arrive via `WM_INPUT`.
- Each `PumpMessages` warps the cursor back to the client-rect centre, so absolute-mode raw input never clamps at a screen edge (see [Message Loop](#message-loop)).

`SetMouseCaptured(false)` reverses each step: unregisters the raw mouse device, releases `ClipCursor` / `ReleaseCapture`, and restores the arrow cursor. Capture is also released automatically on `WM_CAPTURECHANGED` (when another window steals capture) and on `WM_ACTIVATEAPP` with `wParam == FALSE` (when the app loses focus). The latter additionally calls `XRawInput::ClearState` so no key or button appears stuck-down when focus returns.

The keyboard raw-input device (usage page 0x01, usage 0x06) is registered unconditionally during `Initialize` so that `IsKeyDown` works from the very first frame, independent of capture state.

## Raw Input Semantics

`XRawInput` keeps two parallel state arrays for keys and buttons — current and previous — so that edge queries are exact for one frame:

| Query                       | Meaning                                                          |
|-----------------------------|------------------------------------------------------------------|
| `IsKeyDown(vk)`             | Key is currently held.                                           |
| `IsKeyPressed(vk)`          | Key transitioned up → down since the previous `Update`.          |
| `IsKeyReleased(vk)`         | Key transitioned down → up since the previous `Update`.          |
| `IsMouseButtonDown(b)`      | Button is currently held (`b` is 0=L, 1=R, 2=M, 3=X1, 4=X2).     |
| `IsMouseButtonPressed(b)`   | Button transitioned up → down since the previous `Update`.       |
| `IsMouseButtonReleased(b)`  | Button transitioned down → up since the previous `Update`.       |
| `GetMouseDelta(dx, dy)`     | Accumulated mouse motion in pixels since the previous `Update`.  |
| `ConsumeScrollDelta()`      | Accumulated wheel ticks; reads and zeroes in one call.           |

Mouse motion accepts both `MOUSE_MOVE_RELATIVE` and `MOUSE_MOVE_ABSOLUTE` raw packets. Absolute samples are converted to virtual-desktop or primary-screen pixels (per the `MOUSE_VIRTUAL_DESKTOP` flag) and differenced against the previous sample, with the baseline reset each `Update` to keep the cursor centre-warp in `PumpMessages` artefact-free.

`Update` is called for you by `XAppWindow::PumpMessages` when both objects were created via `CreatePlatformWindow`. Call it yourself only when driving a custom message loop. `ClearState` exists for callers that need to drop all held-key / held-button state explicitly — for example, after a modal dialog or some other focus excursion that bypasses the normal `WM_ACTIVATEAPP` path.

## Image Loading

`LoadImageData` decodes an image into a CPU-side pixel buffer suitable for handing to `XGfxDevice::UploadTextureRegion` or any other consumer that wants raw pixels. Two overloads are provided:

```cpp
struct ImageData
{
    uint32_t              Width  = 0;
    uint32_t              Height = 0;
    Canvas::GfxFormat     Format = Canvas::GfxFormat::Unknown;
    std::vector<uint8_t>  Pixels;   // tightly packed, row pitch = Width * bytesPerPixel
};

bool LoadImageData(const wchar_t*    path,
                   Canvas::GfxFormat format,
                   ImageData*        outImage,
                   Canvas::XLogger*  pLogger);

bool LoadImageData(const uint8_t*    data,
                   size_t            byteCount,
                   Canvas::GfxFormat format,
                   ImageData*        outImage,
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

Any other `GfxFormat` value is rejected with an `XLogger` error and `false` return; if you need a new format, extend the switch in `ImageLoader.cpp`.

Both overloads return `false` on any failure (null/empty input, unsupported format, file-open error, decoder error, format-converter error, `CopyPixels` error) and leave `*outImage` cleared. On failure they log an error through the supplied `XLogger`; on success they log an info line with the decoded dimensions and source. A null `XLogger` is allowed — log calls are routed through the standard `LogError` / `LogInfo` helpers, which no-op when the logger is null.

**COM apartment.** WIC requires COM to be initialized on the calling thread. CanvasPlatformWin32 does **not** call `CoInitializeEx` for you; consumers that decode images off the main thread are responsible for initializing COM on those threads themselves.

## Build

CanvasPlatformWin32 is a CMake static library target built as part of the top-level Canvas build:

- Sources: `AppWindow.{h,cpp}`, `RawInput.{h,cpp}`, `ImageLoader.cpp`, `pch.h`, plus the public header `../Inc/CanvasPlatformWin32.h`.
- C++ standard: C++17.
- Compile definitions: `NOMINMAX`, `_WINDOWS`.
- Public dependencies: `GEM`, `windowscodecs` (the WIC import library — public so consumers that statically link this target also get WIC).
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
ImageData img;
if (LoadImageData(L"assets/diffuse.png",
                  Canvas::GfxFormat::R8G8B8A8_UNorm, &img, pLogger))
{
    // img.Width / img.Height / img.Pixels.data() ready to hand to
    // XGfxDevice::UploadTextureRegion.
}
```

Consumers in this repository include [`CanvasModelViewer`](../CanvasModelViewer) and [`CanvasTerrainViewer`](../CanvasTerrainViewer).
