#pragma once

#include <windows.h>

class Application;

class GlobalHotkeys {
public:
    GlobalHotkeys(Application* app);
    ~GlobalHotkeys();

    bool Initialize();
    void Cleanup();
    
    static LRESULT CALLBACK HotkeyWindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

private:
    bool RegisterHotkey(int id, UINT modifiers, UINT vk);
    void UnregisterHotkey(int id);
    
    Application* m_application;
    HWND m_hHotkeyWnd;
    
    static const int HOTKEY_ALT_T = 1;
};