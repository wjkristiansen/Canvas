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
    m_HasLastAbsPos = false;  // reset absolute baseline each frame (pairs with cursor centre-warp)
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
    m_ScrollDelta   = 0;
}

void CRawInput::ProcessRawInput(WPARAM /*wParam*/, LPARAM lParam)
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
            float absX, absY;
            if (mouse.usFlags & MOUSE_VIRTUAL_DESKTOP)
            {
                absX = (mouse.lLastX / 65535.0f) * ::GetSystemMetrics(SM_CXVIRTUALSCREEN);
                absY = (mouse.lLastY / 65535.0f) * ::GetSystemMetrics(SM_CYVIRTUALSCREEN);
            }
            else
            {
                absX = (mouse.lLastX / 65535.0f) * ::GetSystemMetrics(SM_CXSCREEN);
                absY = (mouse.lLastY / 65535.0f) * ::GetSystemMetrics(SM_CYSCREEN);
            }
            if (m_HasLastAbsPos)
            {
                m_MouseDX += absX - m_LastAbsX;
                m_MouseDY += absY - m_LastAbsY;
            }
            m_LastAbsX      = absX;
            m_LastAbsY      = absY;
            m_HasLastAbsPos = true;
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
