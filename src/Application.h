#pragma once

#include <windows.h>
#include <string>
#include <memory>
#include <gdiplus.h>

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
    void OnMouseMove(LPARAM lParam);
    void OnLButtonDown(LPARAM lParam);
    void OnLButtonUp(LPARAM lParam);
    void DrawCustomButton(HDC hdc, HWND hBtn, const std::wstring& text, bool isHovered, bool isPressed);
    bool IsPointInButton(HWND hBtn, POINT pt);
    void InvalidateButton(HWND hBtn);
    void DrawCard(HDC hdc, RECT rect, const std::wstring& title = L"");
    void DrawBackground(HDC hdc, RECT rect);
    void DrawEditBorder(HWND hEdit);
    static LRESULT CALLBACK TreeCanvasSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData);
    
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
    void ScrollCanvas(int direction); // 0=up, 1=down, 2=left, 3=right
    std::wstring GetCurrentWorkingPath();
    void HandleMouseWheelScroll(int delta);

    HINSTANCE m_hInstance;
    HWND m_hWnd;
    HWND m_hDepthEdit;
    HWND m_hPathEdit;
    HWND m_hGenerateBtn;
    HWND m_hCopyBtn;
    HWND m_hSaveBtn;
    HWND m_hTreeCanvas;
    HWND m_hStatusLabel;

    std::unique_ptr<DirectoryTreeBuilder> m_treeBuilder;
    std::unique_ptr<SystemTray> m_systemTray;
    std::unique_ptr<GlobalHotkeys> m_globalHotkeys;

    std::wstring m_treeContent;
    int m_currentDepth;
    bool m_isMinimized;
    bool m_isDefaultDepthValue;
    bool m_hasGeneratedTree;  // Flag to track if tree was generated
    std::wstring m_lastKnownPath;
    
    // GDI+ and custom drawing
    ULONG_PTR m_gdiplusToken;
    HWND m_hoveredButton;
    HWND m_pressedButton;
    
    // Dark theme brushes
    HBRUSH m_hBgBrush;
    HBRUSH m_hEditBrush;
    HBRUSH m_hStaticBrush;
    
    static const UINT_PTR STATUS_TIMER_ID = 1;
    static const UINT_PTR PATH_UPDATE_TIMER_ID = 2;

    static const int MIN_WIDTH = 600;
    static const int MIN_HEIGHT = 400;
};