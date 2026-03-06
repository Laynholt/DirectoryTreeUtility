#include "Application.h"
#include "AppInfo.h"

#include <windows.h>

const UINT WM_ACTIVATE_INSTANCE = WM_USER + 200;

BOOL CALLBACK EnumWindowsProc(HWND hWnd, LPARAM lParam) {
    UNREFERENCED_PARAMETER(lParam);
    wchar_t className[256];
    if (GetClassName(hWnd, className, 256) > 0) {
        if (wcscmp(className, AppInfo::kWindowClass) == 0) {
            PostMessage(hWnd, WM_ACTIVATE_INSTANCE, 0, 0);
            return FALSE;
        }
    }
    return TRUE;
}

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
                     _In_opt_ HINSTANCE hPrevInstance,
                     _In_ LPWSTR lpCmdLine,
                     _In_ int nCmdShow) {
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);
    UNREFERENCED_PARAMETER(nCmdShow);

    HANDLE hMutex = CreateMutex(nullptr, FALSE, AppInfo::kMutexName);
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        EnumWindows(EnumWindowsProc, 0);
        CloseHandle(hMutex);
        return 0;
    }

    Application app;
    if (!app.Initialize(hInstance)) {
        MessageBox(nullptr, L"Не удалось инициализировать приложение", L"Ошибка", MB_OK | MB_ICONERROR);
        CloseHandle(hMutex);
        return -1;
    }

    app.ShowWindow(true);

    HWND hWnd = app.GetMainWindow();
    SetWindowPos(hWnd, nullptr, 0, 0, 0, 0, 
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
    UpdateWindow(hWnd);

    int result = app.Run();
    app.Shutdown();
    
    CloseHandle(hMutex);
    return result;
}
