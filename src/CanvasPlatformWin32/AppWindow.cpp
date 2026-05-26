#include "pch.h"
#include "AppWindow.h"
#include "RawInput.h"

namespace Canvas::Platform::Win32 {

//---------------------------------------------------------------------------------------------
// CImpl — ThinWin window subclass that manages the HWND, FPS mouse capture, and
// raw-input forwarding on behalf of the privately bound CRawInput (if any).
//---------------------------------------------------------------------------------------------
class CAppWindow::CImpl : public ThinWin::CWindow
{
public:
    CImpl(CRawInput* rawInput,
          LPCSTR title, HINSTANCE hInst, DWORD style, int x, int y, int w, int h)
        : CWindow(title, hInst, style, x, y, w, h)
        , m_RawInput(rawInput)
    {}

    bool IsMouseCaptured() const { return m_Captured; }

    void SetMouseCaptured(bool captured)
    {
        if (captured && !m_Captured)
        {
            ::SetCapture(m_hWnd);
            ::SetCursor(NULL);

            RECT rc;
            ::GetClientRect(m_hWnd, &rc);
            ::MapWindowPoints(m_hWnd, nullptr, reinterpret_cast<POINT*>(&rc), 2);
            ::ClipCursor(&rc);

            RAWINPUTDEVICE rid{};
            rid.usUsagePage = 0x01; // HID_USAGE_PAGE_GENERIC
            rid.usUsage     = 0x02; // HID_USAGE_GENERIC_MOUSE
            rid.dwFlags     = 0;
            rid.hwndTarget  = m_hWnd;
            ::RegisterRawInputDevices(&rid, 1, sizeof(rid));

            m_Captured = true;
        }
        else if (!captured && m_Captured)
        {
            m_Captured = false;

            RAWINPUTDEVICE rid{};
            rid.usUsagePage = 0x01;
            rid.usUsage     = 0x02;
            rid.dwFlags     = RIDEV_REMOVE;
            rid.hwndTarget  = nullptr;
            ::RegisterRawInputDevices(&rid, 1, sizeof(rid));

            ::ClipCursor(nullptr);
            ::ReleaseCapture();
            ::SetCursor(LoadCursor(NULL, IDC_ARROW));
        }
    }

protected:
    LRESULT WindowProc(UINT uMsg, WPARAM wParam, LPARAM lParam) override
    {
        switch (uMsg)
        {
        case WM_INPUT:
            if (m_RawInput)
                m_RawInput->ProcessRawInput(wParam, lParam);
            break;

        case WM_MOUSEMOVE:
            if (m_Captured) { ::SetCursor(NULL); return 0; }
            break;

        case WM_SETCURSOR:
            if (LOWORD(lParam) == HTCLIENT)
            {
                ::SetCursor(m_Captured ? NULL : LoadCursor(NULL, IDC_ARROW));
                return TRUE;
            }
            break;

        case WM_CAPTURECHANGED:
            if (reinterpret_cast<HWND>(lParam) != m_hWnd)
                SetMouseCaptured(false);
            return 0;

        case WM_ACTIVATEAPP:
            if (!wParam)
            {
                SetMouseCaptured(false);
                if (m_RawInput) m_RawInput->ClearState();
            }
            break;

        case WM_DESTROY:
            ::PostQuitMessage(0);
            return 0;
        }

        return CWindow::WindowProc(uMsg, wParam, lParam);
    }

private:
    CRawInput* const m_RawInput;  // raw pointer; CAppWindow::m_pRawInput TGemPtr guarantees lifetime
    bool        m_Captured = false;
};

//---------------------------------------------------------------------------------------------
// CAppWindow
//---------------------------------------------------------------------------------------------

CAppWindow::CAppWindow(const AppWindowDesc& desc, CRawInput* pRawInput)
    : m_Desc(desc), m_pRawInput(pRawInput)
{}

Gem::Result CAppWindow::Initialize()
{
    HINSTANCE hInst = m_Desc.hInstance ? m_Desc.hInstance : ::GetModuleHandleA(nullptr);

    ThinWin::CWindow::RegisterWindowClass(hInst, nullptr, NULL, m_Desc.hIcon, m_Desc.hIconSm);

    m_pImpl = std::make_unique<CImpl>(
        m_pRawInput.Get(),
        m_Desc.Title, hInst, m_Desc.Style,
        CW_USEDEFAULT, 0, m_Desc.Width, m_Desc.Height);

    if (m_pRawInput)
    {
        // Register keyboard raw input unconditionally so key queries work from the start.
        RAWINPUTDEVICE kbdRid{};
        kbdRid.usUsagePage = 0x01; // HID_USAGE_PAGE_GENERIC
        kbdRid.usUsage     = 0x06; // HID_USAGE_GENERIC_KEYBOARD
        kbdRid.dwFlags     = 0;
        kbdRid.hwndTarget  = m_pImpl->m_hWnd;
        ::RegisterRawInputDevices(&kbdRid, 1, sizeof(kbdRid));
    }

    return Gem::Result::Success;
}

GEMMETHODIMP_(void) CAppWindow::Show(int nCmdShow)
{
    m_pImpl->ShowWindow(nCmdShow);
    m_pImpl->UpdateWindow();
}

GEMMETHODIMP_(HWND) CAppWindow::GetHWND()
{
    return m_pImpl ? m_pImpl->m_hWnd : NULL;
}

GEMMETHODIMP_(bool) CAppWindow::HasFocus()
{
    if (!m_pImpl) return false;
    HWND fg     = ::GetForegroundWindow();
    HWND fgRoot = fg ? ::GetAncestor(fg, GA_ROOT) : nullptr;
    HWND hWnd   = m_pImpl->m_hWnd;
    return fg == hWnd || fgRoot == hWnd || (fg && ::IsChild(hWnd, fg));
}

GEMMETHODIMP_(bool) CAppWindow::PumpMessages()
{
    if (!m_pImpl) return false;

    if (m_pRawInput)
        m_pRawInput->Update();

    MSG msg;
    while (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
    {
        ::TranslateMessage(&msg);
        ::DispatchMessage(&msg);
        if (msg.message == WM_QUIT)
            return false;
    }

    // Warp cursor to window centre after draining so absolute-mode deltas are never
    // constrained by screen boundaries.  m_HasLastAbsPos was reset by Update() above,
    // so the next absolute WM_INPUT sets a fresh baseline near centre rather than
    // generating a spurious large delta.
    if (m_pImpl->IsMouseCaptured())
    {
        RECT rc;
        ::GetClientRect(m_pImpl->m_hWnd, &rc);
        POINT c = { (rc.left + rc.right) / 2, (rc.top + rc.bottom) / 2 };
        ::ClientToScreen(m_pImpl->m_hWnd, &c);
        ::SetCursorPos(c.x, c.y);
    }

    return true;
}

GEMMETHODIMP_(void) CAppWindow::SetMouseCaptured(bool captured)
{
    if (m_pImpl) m_pImpl->SetMouseCaptured(captured);
}

GEMMETHODIMP_(bool) CAppWindow::IsMouseCaptured()
{
    return m_pImpl && m_pImpl->IsMouseCaptured();
}

//---------------------------------------------------------------------------------------------
// Factories
//---------------------------------------------------------------------------------------------

Gem::Result CreatePlatformWindow(const AppWindowDesc& desc, XAppWindow** ppWindow, XRawInput** ppInput)
{
    if (!ppWindow || !ppInput) return Gem::Result::BadPointer;

    Gem::TGemPtr<CRawInput> pRawInput;
    Gem::Result hr = Gem::TGenericImpl<CRawInput>::Create(&pRawInput);
    if (Gem::Failed(hr)) return hr;

    Gem::TGemPtr<CAppWindow> pWindow;
    hr = Gem::TGenericImpl<CAppWindow>::Create(&pWindow, desc, pRawInput.Get());
    if (Gem::Failed(hr)) return hr;

    hr = pWindow->QueryInterface(ppWindow);
    if (Gem::Failed(hr)) return hr;

    return pRawInput->QueryInterface(ppInput);
}

Gem::Result CreateAppWindow(const AppWindowDesc& desc, XAppWindow** ppWindow)
{
    if (!ppWindow) return Gem::Result::BadPointer;
    Gem::TGemPtr<CAppWindow> pImpl;
    Gem::Result hr = Gem::TGenericImpl<CAppWindow>::Create(&pImpl, desc);
    if (Gem::Failed(hr)) return hr;
    return pImpl->QueryInterface(ppWindow);
}

} // namespace Canvas::Platform::Win32
