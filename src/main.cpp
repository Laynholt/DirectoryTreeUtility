#include "Application.h"
#include <windows.h>

const wchar_t* MUTEX_NAME = L"DirectoryTreeUtility_SingleInstance";
const wchar_t* WINDOW_CLASS_NAME = L"DirectoryTreeUtilityClass";
const UINT WM_ACTIVATE_INSTANCE = WM_USER + 200;

BOOL CALLBACK EnumWindowsProc(HWND hWnd, LPARAM lParam) {
    wchar_t className[256];
    if (GetClassName(hWnd, className, 256) > 0) {
        if (wcscmp(className, WINDOW_CLASS_NAME) == 0) {
            // Found existing window - send message to show it properly
            // This will use Application::ShowWindow() method which handles m_isMinimized flag
            PostMessage(hWnd, WM_ACTIVATE_INSTANCE, 0, 0);
            
            return FALSE; // Stop enumeration
        }
    }
    return TRUE; // Continue enumeration
}

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
                     _In_opt_ HINSTANCE hPrevInstance,
                     _In_ LPWSTR lpCmdLine,
                     _In_ int nCmdShow) {
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);
    UNREFERENCED_PARAMETER(nCmdShow);

    // Check if another instance is already running
    HANDLE hMutex = CreateMutex(nullptr, FALSE, MUTEX_NAME);
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        // Another instance is running - find and activate its window
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

    // Show the window after successful initialization
    app.ShowWindow(true);
    
    // Force update of the non-client area to ensure title bar buttons appear
    HWND hWnd = app.GetMainWindow();
    SetWindowPos(hWnd, nullptr, 0, 0, 0, 0, 
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
    UpdateWindow(hWnd);

    int result = app.Run();
    app.Shutdown();
    
    // Release the mutex
    CloseHandle(hMutex);
    return result;
}