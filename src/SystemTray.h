#pragma once

#include <windows.h>
#include <shellapi.h>
#include <functional>

class Application;

class SystemTray {
public:
    SystemTray(Application* app);
    ~SystemTray();

    bool Initialize();
    void Cleanup();
    
    void AddToTray();
    void RemoveFromTray();
    void ShowContextMenu(int x, int y);

    static LRESULT CALLBACK TrayWindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

private:
    void CreateTrayIcon();
    void CreateContextMenu();
    
    Application* m_application;
    HWND m_hTrayWnd;
    NOTIFYICONDATA m_nid;
    HMENU m_hContextMenu;
    HICON m_hIcon;

    static const UINT WM_TRAYICON = WM_USER + 1;
    static const UINT TRAY_ID = 1;
    
    enum MenuItems {
        ID_SHOW = 1000,
        ID_EXIT = 1001
    };
};