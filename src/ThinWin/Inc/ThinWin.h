//================================================================================================
// ThinWin
//================================================================================================

#pragma once

namespace ThinWin
{

class CWindow
{
    static LRESULT CALLBACK WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

public:
    static ATOM RegisterWindowClass(HINSTANCE hInstance, PCSTR pszMenuName = nullptr, HCURSOR hCursor = NULL, HICON hIcon = NULL, HICON hIconSm = NULL, HBRUSH hbrBackground = (HBRUSH)(COLOR_WINDOW + 1));

public:
    HWND m_hWnd = NULL;

    CWindow(
        _In_opt_ LPCSTR lpWindowName,
        _In_ HINSTANCE hInstance, 
        _In_ DWORD dwStyle = WS_OVERLAPPEDWINDOW,
        _In_ int X = CW_USEDEFAULT,
        _In_ int Y = 0,
        _In_ int nWidth = CW_USEDEFAULT,
        _In_ int nHeight = 0,
        _In_opt_ HWND hWndParent = NULL,
        _In_opt_ HMENU hMenu = NULL,
        _In_opt_ LPVOID lpParam = nullptr,
        _In_ DWORD dwExStyle = 0
    );

    bool ShowWindow(int nCmdShow) { return static_cast<bool>(::ShowWindow(m_hWnd, nCmdShow)); }
    bool UpdateWindow() { return static_cast<bool>(::UpdateWindow(m_hWnd)); }

    virtual ~CWindow()
    {
        if (m_hWnd)
        {
            DestroyWindow(m_hWnd);
        }
    }

    virtual LRESULT WindowProc(UINT uMsg, WPARAM wParam, LPARAM lParam);
};

}