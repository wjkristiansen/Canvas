//================================================================================================
// ThinWin
//================================================================================================

#pragma once

namespace ThinWin
{

class CWindow
{
    static LRESULT CALLBACK WndProcCb(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

public:
    HWND m_hWnd = NULL;

    CWindow();
    ~CWindow(); 

    static ATOM RegisterWindowClass(DWORD dwExStyle, PCWSTR szWindowName, HINSTANCE hInstance, HICON hIcon)

    virtual LRESULT WindowProc(UINT uMsg, WPARAM wParam, LPARAM lParam);

    
    static CWindow *Create();   
};

}