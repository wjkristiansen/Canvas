//================================================================================================
// ThinWin
//================================================================================================

#include "stdafx.h"

using namespace ThinWin;

//------------------------------------------------------------------------------------------------
ATOM CWindow::RegisterWindowClass(HINSTANCE hInstance, PCWSTR pszMenuName, HCURSOR hCursor, HICON hIcon, HICON hIconSm, HBRUSH hbrBackground)
{
    WNDCLASSEXW wcex = {};

    wcex.cbSize = sizeof(WNDCLASSEX);

    wcex.style          = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc    = CWindow::WndProc;
    wcex.cbClsExtra     = 0;
    wcex.cbWndExtra     = sizeof(size_t); // Make room for the CWindow *
    wcex.hInstance      = hInstance;
    wcex.hIcon          = hIcon;
    wcex.hCursor        = hCursor;
    wcex.hbrBackground  = hbrBackground;
    wcex.lpszMenuName   = nullptr;
    wcex.lpszClassName  = L"ThinWinDefault";
    wcex.hIconSm        = hIconSm;

    return RegisterClassExW(&wcex);
}

//------------------------------------------------------------------------------------------------
LRESULT CALLBACK CWindow::WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_CREATE:
        {
            LPCREATESTRUCT pCreateStruct = reinterpret_cast<LPCREATESTRUCT>(lParam);
            // Handle WM_CREATE to set the CWindow pointer
            SetWindowLongPtrW(hWnd, 0, reinterpret_cast<LPARAM>(pCreateStruct->lpCreateParams));
            break;
        }
    }

    CWindow *pWindow = reinterpret_cast<CWindow *>(GetWindowLongPtrW(hWnd, 0));
    if (pWindow)
    {
        return pWindow->WindowProc(uMsg, wParam, lParam);
    }

    return DefWindowProcW(hWnd, uMsg, wParam, lParam);
}

//------------------------------------------------------------------------------------------------
CWindow::CWindow(
    _In_opt_ LPCWSTR lpWindowName,
    HINSTANCE hInstance, 
    _In_ DWORD dwStyle,
    _In_ int X,
    _In_ int Y,
    _In_ int nWidth,
    _In_ int nHeight,
    _In_opt_ HWND hWndParent,
    _In_opt_ HMENU hMenu,
    _In_opt_ LPVOID lpParam,
    _In_ DWORD dwExStyle
)
{
    HWND hWnd = CreateWindowExW(dwExStyle, L"ThinWinDefault", lpWindowName, dwStyle, X, Y, nWidth, nHeight, hWndParent, hMenu, hInstance, this);
    if (hWnd == NULL)
    {
        throw(_com_error(HRESULT_FROM_WIN32(GetLastError())));
    }

    m_hWnd = hWnd;
}

//------------------------------------------------------------------------------------------------
LRESULT CWindow::WindowProc(UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_CREATE:
        OutputDebugString(L"WM_CREATE");
        break;
    case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(m_hWnd, &ps);
            // TODO: Add any drawing code that uses hdc here...
            EndPaint(m_hWnd, &ps);
        }
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(m_hWnd, message, wParam, lParam);
    }
    return 0;
}