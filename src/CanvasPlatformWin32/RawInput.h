#pragma once
#include "CanvasPlatformWin32.h"

namespace Canvas::Platform::Win32 {

class CRawInput : public XRawInput
{
public:
    CRawInput() = default;

    Gem::Result Initialize() { return Gem::Result::Success; }
    void Uninitialize() {}

    BEGIN_GEM_INTERFACE_MAP()
        GEM_INTERFACE_ENTRY(XRawInput)
    END_GEM_INTERFACE_MAP()

    GEMMETHOD_(void, Update)() final;
    GEMMETHOD_(void, ClearState)() final;
    GEMMETHOD_(bool, IsKeyDown)(int vk) final;
    GEMMETHOD_(bool, IsKeyPressed)(int vk) final;
    GEMMETHOD_(bool, IsKeyReleased)(int vk) final;
    GEMMETHOD_(void, GetMouseDelta)(float& dx, float& dy) final;
    GEMMETHOD_(bool, IsMouseButtonDown)(int button) final;
    GEMMETHOD_(bool, IsMouseButtonPressed)(int button) final;
    GEMMETHOD_(bool, IsMouseButtonReleased)(int button) final;
    GEMMETHOD_(int, ConsumeScrollDelta)() final;

    // Internal — called by CImpl::WindowProc when WM_INPUT arrives.  hwnd is the
    // window that owns the capture; needed as the recentre target under RDP.
    void ProcessRawInput(HWND hwnd, WPARAM wParam, LPARAM lParam);

    // Internal — true when running inside a Remote Desktop session.  Sampled by
    // Update() each frame so it tracks RDP connect/disconnect transitions.  When
    // true, CAppWindow::PumpMessages must NOT issue its own per-frame warp; the
    // recentre is driven from ProcessRawInput's safe-area logic instead.
    bool IsRemoteSession() const { return m_IsRemoteSession; }

    // Internal — drop the persistent RDP baseline so the next absolute sample
    // re-seeds without emitting a spurious delta.  Called on capture changes.
    void ResetRemoteBaseline() { m_HasRawPos = false; }

private:
    // Recentre the cursor to the window centre under RDP.  Jitters the target and
    // issues SetCursorPos repeatedly because RDP coalesces identical SetCursorPos
    // calls and silently drops a warp whose target matches the last one sent.
    void WarpToWindowCentre(HWND hwnd);

    float  m_MouseDX       = 0.0f;
    float  m_MouseDY       = 0.0f;
    float  m_LastAbsX      = 0.0f;
    float  m_LastAbsY      = 0.0f;
    bool   m_HasLastAbsPos = false;
    bool   m_IsRemoteSession = false;
    int    m_LastRawX      = 0;     // persistent absolute baseline (RDP path)
    int    m_LastRawY      = 0;
    bool   m_HasRawPos     = false;
    int    m_Wobble        = 0;     // alternating warp-target offset (RDP)
    int    m_ScrollDelta   = 0;
    bool   m_KeyCurrent[256]{};
    bool   m_KeyPrev[256]{};
    bool   m_BtnCurrent[5]{};
    bool   m_BtnPrev[5]{};
};

} // namespace Canvas::Platform::Win32
