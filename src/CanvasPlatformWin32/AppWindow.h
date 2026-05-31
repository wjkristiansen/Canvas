#pragma once
#include "CanvasPlatformWin32.h"
#include "RawInput.h"
#include <ThinWin.h>
#include <memory>

namespace Canvas::Platform::Win32 {

class CAppWindow : public XAppWindow
{
public:
    explicit CAppWindow(const AppWindowDesc& desc, CRawInput* pRawInput = nullptr);

    Gem::Result Initialize();
    void Uninitialize() {}

    BEGIN_GEM_INTERFACE_MAP()
        GEM_INTERFACE_ENTRY(XAppWindow)
    END_GEM_INTERFACE_MAP()

    GEMMETHOD_(void, Show)(int nCmdShow) final;
    GEMMETHOD_(HWND, GetHWND)() final;
    GEMMETHOD_(bool, HasFocus)() final;
    GEMMETHOD_(bool, PumpMessages)() final;
    GEMMETHOD_(void, SetMouseCaptured)(bool captured) final;
    GEMMETHOD_(bool, IsMouseCaptured)() final;
    GEMMETHOD_(void, SetWindowSize)(int width, int height) final;
    GEMMETHOD_(void, GetClientSize)(int& width, int& height) final;
    GEMMETHOD_(void, SetFullscreen)(bool fullscreen) final;
    GEMMETHOD_(bool, IsFullscreen)() final;

private:
    class CImpl;
    AppWindowDesc             m_Desc;
    Gem::TGemPtr<CRawInput>   m_pRawInput;  // declared before m_pImpl: CImpl is destroyed first,
    std::unique_ptr<CImpl>    m_pImpl;      // so CRawInput is always valid during CImpl's lifetime
};

} // namespace Canvas::Platform::Win32
