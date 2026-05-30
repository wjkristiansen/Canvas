#include "pch.h"
#include "RawInput.h"
#include <cstring>

namespace Canvas::Platform::Win32 {

static const struct { USHORT downFlag; USHORT upFlag; } kBtnFlags[5] =
{
    { RI_MOUSE_BUTTON_1_DOWN, RI_MOUSE_BUTTON_1_UP },
    { RI_MOUSE_BUTTON_2_DOWN, RI_MOUSE_BUTTON_2_UP },
    { RI_MOUSE_BUTTON_3_DOWN, RI_MOUSE_BUTTON_3_UP },
    { RI_MOUSE_BUTTON_4_DOWN, RI_MOUSE_BUTTON_4_UP },
    { RI_MOUSE_BUTTON_5_DOWN, RI_MOUSE_BUTTON_5_UP },
};

GEMMETHODIMP_(void) CRawInput::Update()
{
    memcpy(m_KeyPrev, m_KeyCurrent, sizeof(m_KeyCurrent));
    memcpy(m_BtnPrev, m_BtnCurrent, sizeof(m_BtnCurrent));
    m_MouseDX       = 0.0f;
    m_MouseDY       = 0.0f;
    m_HasLastAbsPos = false;  // reset per-frame baseline for the local absolute (tablet) path

    // Track RDP connect/disconnect so the absolute path can switch strategies.
    m_IsRemoteSession = ::GetSystemMetrics(SM_REMOTESESSION) != 0;
}

GEMMETHODIMP_(void) CRawInput::ClearState()
{
    memset(m_KeyCurrent, 0, sizeof(m_KeyCurrent));
    memset(m_KeyPrev,    0, sizeof(m_KeyPrev));
    memset(m_BtnCurrent, 0, sizeof(m_BtnCurrent));
    memset(m_BtnPrev,    0, sizeof(m_BtnPrev));
    m_MouseDX       = 0.0f;
    m_MouseDY       = 0.0f;
    m_HasLastAbsPos = false;
    m_HasRawPos     = false;
    m_ScrollDelta   = 0;
}

void CRawInput::WarpToWindowCentre(HWND hwnd)
{
    RECT rc;
    if (!::GetClientRect(hwnd, &rc))
        return;

    // Alternate the target X by a pixel each warp.  RDP coalesces SetCursorPos
    // calls and ignores a warp whose target equals the previous one, so the
    // target must keep changing.
    POINT c{ (rc.left + rc.right) / 2 + m_Wobble, (rc.top + rc.bottom) / 2 };
    ::ClientToScreen(hwnd, &c);

    // Triple-call with a 1px jog for the same anti-coalescing reason.
    ::SetCursorPos(c.x, c.y);
    ::SetCursorPos(c.x + 1, c.y);
    ::SetCursorPos(c.x, c.y);

    m_Wobble = (m_Wobble == 0) ? 1 : 0;
}

void CRawInput::ProcessRawInput(HWND hwnd, WPARAM /*wParam*/, LPARAM lParam)
{
    UINT dwSize = sizeof(RAWINPUT);
    alignas(RAWINPUT) BYTE buffer[sizeof(RAWINPUT)];
    HRAWINPUT hRaw = reinterpret_cast<HRAWINPUT>(lParam);
    if (::GetRawInputData(hRaw, RID_INPUT, buffer, &dwSize, sizeof(RAWINPUTHEADER)) == UINT(-1))
        return;

    const auto* raw = reinterpret_cast<const RAWINPUT*>(buffer);

    if (raw->header.dwType == RIM_TYPEMOUSE)
    {
        const RAWMOUSE& mouse = raw->data.mouse;

        if (mouse.usFlags & MOUSE_MOVE_ABSOLUTE)
        {
            const bool virtualDesktop = (mouse.usFlags & MOUSE_VIRTUAL_DESKTOP) != 0;
            const int  screenW = ::GetSystemMetrics(virtualDesktop ? SM_CXVIRTUALSCREEN : SM_CXSCREEN);
            const int  screenH = ::GetSystemMetrics(virtualDesktop ? SM_CYVIRTUALSCREEN : SM_CYSCREEN);

            if (m_IsRemoteSession && hwnd)
            {
                // Over RDP the mouse arrives as an absolute virtual-desktop
                // position rather than a relative delta, so a plain centre-warp
                // is fought by the OS: it clamps the cursor to the desktop edge
                // and (because the captured cursor is hidden) the warp may be a
                // no-op.  Mirror SDL's relative-mode-over-RDP handling: difference
                // against a persistent baseline, and when the cursor nears a
                // screen edge recentre it instead of emitting motion.  See
                // README.md "Remote Desktop" section.
                const int x = static_cast<int>((mouse.lLastX / 65535.0f) * screenW);
                const int y = static_cast<int>((mouse.lLastY / 65535.0f) * screenH);

                if (!m_HasRawPos)
                {
                    m_LastRawX  = x;
                    m_LastRawY  = y;
                    m_HasRawPos = true;
                }

                const int relX = x - m_LastRawX;
                const int relY = y - m_LastRawY;

                const float fx = (screenW > 0) ? static_cast<float>(x) / screenW : 0.5f;
                const float fy = (screenH > 0) ? static_cast<float>(y) / screenH : 0.5f;

                // Recentre when the cursor approaches any screen edge.  y < 32
                // also catches the RDP connection/title bar strip at the top of
                // the remote desktop.
                const bool nearEdge =
                    fx <= 0.01f || fx >= 0.99f ||
                    fy <= 0.01f || fy >= 0.99f ||
                    y < 32;

                if (nearEdge)
                {
                    WarpToWindowCentre(hwnd);
                }
                else
                {
                    // Reject the large jump produced by a warp (the next sample
                    // lands far from the pre-warp edge position).  Genuine motion
                    // is always far below this threshold.
                    const int maxMotion = (screenH > 0) ? screenH / 6 : 256;
                    if (relX > -maxMotion && relX < maxMotion &&
                        relY > -maxMotion && relY < maxMotion)
                    {
                        m_MouseDX += static_cast<float>(relX);
                        m_MouseDY += static_cast<float>(relY);
                    }
                }

                m_LastRawX = x;
                m_LastRawY = y;
            }
            else
            {
                // Local absolute device (e.g. tablet/touch digitizer): difference
                // within the frame against the per-frame baseline.
                const float absX = (mouse.lLastX / 65535.0f) * screenW;
                const float absY = (mouse.lLastY / 65535.0f) * screenH;
                if (m_HasLastAbsPos)
                {
                    m_MouseDX += absX - m_LastAbsX;
                    m_MouseDY += absY - m_LastAbsY;
                }
                m_LastAbsX      = absX;
                m_LastAbsY      = absY;
                m_HasLastAbsPos = true;
            }
        }
        else
        {
            m_MouseDX += static_cast<float>(mouse.lLastX);
            m_MouseDY += static_cast<float>(mouse.lLastY);
        }

        for (int i = 0; i < 5; ++i)
        {
            if (mouse.usButtonFlags & kBtnFlags[i].downFlag) m_BtnCurrent[i] = true;
            if (mouse.usButtonFlags & kBtnFlags[i].upFlag)   m_BtnCurrent[i] = false;
        }

        if (mouse.usButtonFlags & RI_MOUSE_WHEEL)
            m_ScrollDelta += static_cast<SHORT>(mouse.usButtonData) / WHEEL_DELTA;
    }
    else if (raw->header.dwType == RIM_TYPEKEYBOARD)
    {
        const RAWKEYBOARD& kb = raw->data.keyboard;
        bool down = !(kb.Flags & RI_KEY_BREAK);
        USHORT vk  = kb.VKey;
        if (vk < 256)
            m_KeyCurrent[vk] = down;
    }
}

GEMMETHODIMP_(bool) CRawInput::IsKeyDown(int vk)
{
    return (vk >= 0 && vk < 256) && m_KeyCurrent[vk];
}

GEMMETHODIMP_(bool) CRawInput::IsKeyPressed(int vk)
{
    return (vk >= 0 && vk < 256) && m_KeyCurrent[vk] && !m_KeyPrev[vk];
}

GEMMETHODIMP_(bool) CRawInput::IsKeyReleased(int vk)
{
    return (vk >= 0 && vk < 256) && !m_KeyCurrent[vk] && m_KeyPrev[vk];
}

GEMMETHODIMP_(void) CRawInput::GetMouseDelta(float& dx, float& dy)
{
    dx = m_MouseDX;
    dy = m_MouseDY;
}

GEMMETHODIMP_(bool) CRawInput::IsMouseButtonDown(int button)
{
    return (button >= 0 && button < 5) && m_BtnCurrent[button];
}

GEMMETHODIMP_(bool) CRawInput::IsMouseButtonPressed(int button)
{
    return (button >= 0 && button < 5) && m_BtnCurrent[button] && !m_BtnPrev[button];
}

GEMMETHODIMP_(bool) CRawInput::IsMouseButtonReleased(int button)
{
    return (button >= 0 && button < 5) && !m_BtnCurrent[button] && m_BtnPrev[button];
}

GEMMETHODIMP_(int) CRawInput::ConsumeScrollDelta()
{
    int v = m_ScrollDelta;
    m_ScrollDelta = 0;
    return v;
}

} // namespace Canvas::Platform::Win32
