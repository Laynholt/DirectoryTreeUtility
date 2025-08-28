#pragma once

#include <windows.h>
#include <string>
#include <memory>

class DirectoryTreeBuilder;
class SystemTray;
class GlobalHotkeys;

class Application {
public:
    Application();
    ~Application();

    bool Initialize(HINSTANCE hInstance);
    int Run();
    void Shutdown();

    HWND GetMainWindow() const { return m_hWnd; }
    void ShowWindow(bool show = true);
    void ToggleVisibility();

private:
    static LRESULT CALLBACK WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);

    void CreateControls();
    void OnResize(int width, int height);
    void OnPaint();
    void OnCommand(int commandId);
    void OnKeyDown(WPARAM key, LPARAM flags);
    
    void GenerateTree();
    void CopyToClipboard();
    void SaveToFile();
    void UpdateCurrentPath();
    void ShowStatusMessage(const std::wstring& message);
    void HandleNumberInput(wchar_t digit);
    void HandleBackspace();
    void HandleMinusKey();
    void HandleDepthIncrement();
    void HandleDepthDecrement();
    static void CALLBACK StatusTimerProc(HWND hWnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime);

    HINSTANCE m_hInstance;
    HWND m_hWnd;
    HWND m_hDepthEdit;
    HWND m_hPathEdit;
    HWND m_hGenerateBtn;
    HWND m_hCopyBtn;
    HWND m_hSaveBtn;
    HWND m_hTreeCanvas;
    HWND m_hScrollV;
    HWND m_hScrollH;
    HWND m_hStatusLabel;

    std::unique_ptr<DirectoryTreeBuilder> m_treeBuilder;
    std::unique_ptr<SystemTray> m_systemTray;
    std::unique_ptr<GlobalHotkeys> m_globalHotkeys;

    std::wstring m_treeContent;
    int m_currentDepth;
    bool m_isMinimized;
    bool m_isDefaultDepthValue;
    std::wstring m_lastKnownPath;
    
    static const UINT_PTR STATUS_TIMER_ID = 1;
    static const UINT_PTR PATH_UPDATE_TIMER_ID = 2;

    static const int MIN_WIDTH = 600;
    static const int MIN_HEIGHT = 400;
};