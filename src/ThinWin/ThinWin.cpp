//================================================================================================
// ThinWin
//================================================================================================

#include "stdafx.h"

using namespace ThinWin;

ATOM CWindow::RegisterWindowClass(DWORD dwExStyle, PCWSTR szWindowName, HINSTANCE hInstance, HICON hIcon = NULL, HCURSOR hCursor = NULL, HBRUSH hbrBackground = )
{
    WNDCLASSEX WndClass;
    WndClass.cbSize = sizeof(WndClass);
    WndClass.style = CS_HREDRAW | CS_VREDRAW;
    WndClass.lpfnWndProc = WndProcCb;
    WndClass.cbClsExtra = 0;
    WndClass.cbWndExtra = sizeof(size_t);
    WndClass.hInstance = hInstance;
    WndClass.hIcon = hIcon;
    WndClass.hbrBackground = hbrBackground;
}