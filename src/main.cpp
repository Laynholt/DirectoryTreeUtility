#include "Application.h"
#include <windows.h>

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
                     _In_opt_ HINSTANCE hPrevInstance,
                     _In_ LPWSTR lpCmdLine,
                     _In_ int nCmdShow) {
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);
    UNREFERENCED_PARAMETER(nCmdShow);

    Application app;
    if (!app.Initialize(hInstance)) {
        MessageBox(nullptr, L"Не удалось инициализировать приложение", L"Ошибка", MB_OK | MB_ICONERROR);
        return -1;
    }

    // Show the window after successful initialization
    app.ShowWindow(true);
    
    // Force update of the non-client area to ensure title bar buttons appear
    HWND hWnd = app.GetMainWindow();
    SetWindowPos(hWnd, nullptr, 0, 0, 0, 0, 
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
    UpdateWindow(hWnd);

    int result = app.Run();
    app.Shutdown();
    return result;
}