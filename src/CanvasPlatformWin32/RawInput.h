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

    // Internal — called by CAppWindow::PumpMessages when WM_INPUT arrives.
    void ProcessRawInput(WPARAM wParam, LPARAM lParam);

private:
    float  m_MouseDX       = 0.0f;
    float  m_MouseDY       = 0.0f;
    float  m_LastAbsX      = 0.0f;
    float  m_LastAbsY      = 0.0f;
    bool   m_HasLastAbsPos = false;
    int    m_ScrollDelta   = 0;
    bool   m_KeyCurrent[256]{};
    bool   m_KeyPrev[256]{};
    bool   m_BtnCurrent[5]{};
    bool   m_BtnPrev[5]{};
};

} // namespace Canvas::Platform::Win32
