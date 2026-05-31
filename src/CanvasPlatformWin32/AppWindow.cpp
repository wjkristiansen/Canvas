#include "pch.h"
#include "AppWindow.h"
#include "RawInput.h"

namespace Canvas::Platform::Win32 {

//---------------------------------------------------------------------------------------------
// CImpl - ThinWin window subclass that manages the HWND, exclusive mouse capture,
// and raw-input forwarding on behalf of the privately bound CRawInput (if any).
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

    // A fully transparent cursor used while the mouse is captured.  Hiding the
    // cursor with SetCursor(NULL) breaks SetCursorPos() under Remote Desktop -
    // the recentre warp silently no-ops and the cursor drifts to the desktop
    // edge - so we install an invisible cursor instead of removing it.
    HCURSOR BlankCursor()
    {
        if (!m_BlankCursor)
        {
            int cx = ::GetSystemMetrics(SM_CXCURSOR);
            int cy = ::GetSystemMetrics(SM_CYCURSOR);
            if (cx <= 0) cx = 32;
            if (cy <= 0) cy = 32;

            const int planeBytes = (cx * cy) / 8;
            BYTE andPlane[1024];   // AND = 1 everywhere -> transparent
            BYTE xorPlane[1024];   // XOR = 0 everywhere -> no inversion
            if (planeBytes <= static_cast<int>(sizeof(andPlane)))
            {
                ::memset(andPlane, 0xFF, planeBytes);
                ::memset(xorPlane, 0x00, planeBytes);
                m_BlankCursor = ::CreateCursor(
                    ::GetModuleHandleA(nullptr), 0, 0, cx, cy, andPlane, xorPlane);
            }
        }
        return m_BlankCursor;
    }

    void SetMouseCaptured(bool captured)
    {
        if (captured && !m_Captured)
        {
            ::SetCapture(m_hWnd);
            ::SetCursor(BlankCursor());

            ClipCursorToClient();

            RAWINPUTDEVICE rid{};
            rid.usUsagePage = 0x01; // HID_USAGE_PAGE_GENERIC
            rid.usUsage     = 0x02; // HID_USAGE_GENERIC_MOUSE
            rid.dwFlags     = 0;
            rid.hwndTarget  = m_hWnd;
            ::RegisterRawInputDevices(&rid, 1, sizeof(rid));

            if (m_RawInput) m_RawInput->ResetRemoteBaseline();
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

            if (m_RawInput) m_RawInput->ResetRemoteBaseline();
            ::ClipCursor(nullptr);
            ::ReleaseCapture();
            ::SetCursor(LoadCursor(NULL, IDC_ARROW));
        }
    }

    // Clips the cursor to the current client rect.  Used both when capture is
    // first enabled and whenever the window is resized / toggled fullscreen
    // while capture is active (the old clip rect would otherwise be stale).
    void ClipCursorToClient()
    {
        RECT rc;
        ::GetClientRect(m_hWnd, &rc);
        ::MapWindowPoints(m_hWnd, nullptr, reinterpret_cast<POINT*>(&rc), 2);
        ::ClipCursor(&rc);
    }

    void SetWindowSize(int width, int height)
    {
        if (m_Fullscreen || width <= 0 || height <= 0)
            return;

        RECT rc{ 0, 0, width, height };
        const DWORD style   = static_cast<DWORD>(::GetWindowLongPtrW(m_hWnd, GWL_STYLE));
        const DWORD exStyle = static_cast<DWORD>(::GetWindowLongPtrW(m_hWnd, GWL_EXSTYLE));
        ::AdjustWindowRectEx(&rc, style, FALSE, exStyle);
        ::SetWindowPos(m_hWnd, nullptr, 0, 0,
            rc.right - rc.left, rc.bottom - rc.top,
            SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);

        if (m_Captured) ClipCursorToClient();
    }

    bool IsFullscreen() const { return m_Fullscreen; }

    void SetFullscreen(bool fullscreen)
    {
        if (fullscreen == m_Fullscreen)
            return;

        if (fullscreen)
        {
            // Save windowed style and placement so they can be restored on exit.
            m_SavedStyle   = ::GetWindowLongPtrW(m_hWnd, GWL_STYLE);
            m_SavedExStyle = ::GetWindowLongPtrW(m_hWnd, GWL_EXSTYLE);
            m_SavedPlacement.length = sizeof(m_SavedPlacement);
            ::GetWindowPlacement(m_hWnd, &m_SavedPlacement);

            MONITORINFO mi{};
            mi.cbSize = sizeof(mi);
            ::GetMonitorInfo(::MonitorFromWindow(m_hWnd, MONITOR_DEFAULTTONEAREST), &mi);

            ::SetWindowLongPtrW(m_hWnd, GWL_STYLE,
                (m_SavedStyle & ~static_cast<LONG_PTR>(WS_OVERLAPPEDWINDOW)) | WS_POPUP);
            ::SetWindowLongPtrW(m_hWnd, GWL_EXSTYLE,
                m_SavedExStyle & ~static_cast<LONG_PTR>(WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE | WS_EX_DLGMODALFRAME));

            ::SetWindowPos(m_hWnd, HWND_TOP,
                mi.rcMonitor.left, mi.rcMonitor.top,
                mi.rcMonitor.right - mi.rcMonitor.left,
                mi.rcMonitor.bottom - mi.rcMonitor.top,
                SWP_FRAMECHANGED | SWP_NOACTIVATE | SWP_SHOWWINDOW);

            m_Fullscreen = true;
        }
        else
        {
            ::SetWindowLongPtrW(m_hWnd, GWL_STYLE,   m_SavedStyle);
            ::SetWindowLongPtrW(m_hWnd, GWL_EXSTYLE, m_SavedExStyle);
            ::SetWindowPlacement(m_hWnd, &m_SavedPlacement);
            ::SetWindowPos(m_hWnd, nullptr, 0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED | SWP_SHOWWINDOW);

            m_Fullscreen = false;
        }

        if (m_Captured) ClipCursorToClient();
    }

protected:
    LRESULT WindowProc(UINT uMsg, WPARAM wParam, LPARAM lParam) override
    {
        switch (uMsg)
        {
        case WM_INPUT:
            if (m_RawInput)
                m_RawInput->ProcessRawInput(m_hWnd, wParam, lParam);
            break;

        case WM_SYSCHAR:
            // Swallow the system character to suppress it.            return 0;

        case WM_MOUSEMOVE:
            if (m_Captured) { ::SetCursor(BlankCursor()); return 0; }
            break;

        case WM_SETCURSOR:
            if (LOWORD(lParam) == HTCLIENT)
            {
                ::SetCursor(m_Captured ? BlankCursor() : LoadCursor(NULL, IDC_ARROW));
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
    HCURSOR     m_BlankCursor = NULL;

    // Borderless-fullscreen state: windowed style/placement saved on entry.
    bool            m_Fullscreen = false;
    LONG_PTR        m_SavedStyle = 0;
    LONG_PTR        m_SavedExStyle = 0;
    WINDOWPLACEMENT m_SavedPlacement{};

public:
    ~CImpl() override
    {
        if (m_BlankCursor)
            ::DestroyCursor(m_BlankCursor);
    }
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

    // No per-frame cursor warp is needed.  Local pointing devices deliver
    // MOUSE_MOVE_RELATIVE raw deltas that are independent of cursor position, so
    // ClipCursor alone (set in SetMouseCaptured) is enough to contain the hidden
    // cursor.  Under Remote Desktop the recentre is edge-triggered inside
    // ProcessRawInput.  A blanket SetCursorPos here was vestigial warp-mode
    // behaviour and is exactly what broke relative motion over RDP.

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

GEMMETHODIMP_(void) CAppWindow::SetWindowSize(int width, int height)
{
    if (m_pImpl) m_pImpl->SetWindowSize(width, height);
}

GEMMETHODIMP_(void) CAppWindow::GetClientSize(int& width, int& height)
{
    width = 0;
    height = 0;
    if (m_pImpl && m_pImpl->m_hWnd)
    {
        RECT rc{};
        ::GetClientRect(m_pImpl->m_hWnd, &rc);
        width  = rc.right - rc.left;
        height = rc.bottom - rc.top;
    }
}

GEMMETHODIMP_(void) CAppWindow::SetFullscreen(bool fullscreen)
{
    if (m_pImpl) m_pImpl->SetFullscreen(fullscreen);
}

GEMMETHODIMP_(bool) CAppWindow::IsFullscreen()
{
    return m_pImpl && m_pImpl->IsFullscreen();
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
