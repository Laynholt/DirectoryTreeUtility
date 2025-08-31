#pragma once

#include <windows.h>
#include <string>
#include <memory>
#include <map>
#include <thread>
#include <atomic>
#include <mutex>
#include <functional>

class DirectoryTreeBuilder;
class SystemTray;
class GlobalHotkeys;
enum class TreeFormat;

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
    void OnLButtonDown(LPARAM lParam);
    void OnLButtonUp(LPARAM lParam);
    void DrawCustomButton(HDC hdc, HWND hBtn, const std::wstring& text, bool isPressed);
    bool IsPointInButton(HWND hBtn, POINT pt);
    void InvalidateButton(HWND hBtn);
    void RedrawButtonDirect(HWND hBtn);
    void DrawCard(HDC hdc, RECT rect, const std::wstring& title = L"");
    void DrawBackground(HDC hdc, RECT rect);
    void DrawEditBorder(HWND hEdit);
    static LRESULT CALLBACK TreeCanvasSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData);
    static LRESULT CALLBACK DepthEditSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData);
    
    void GenerateTree();
    void GenerateTreeAsync();
    void CancelGeneration();
    void OnTreeGenerationCompleted(const std::wstring& result);
    void OnTreeGenerationError(const std::wstring& error);
    void OnSaveCompleted();
    void OnSaveError(const std::wstring& error);
    void UpdateProgressAnimation();
    void CopyToClipboard();
    void SaveToFile();
    void SaveFileSync(std::wstring&& fileName, const std::wstring& content);
    void SaveFileAsync(std::wstring&& fileName, TreeFormat format);
    void UpdateCurrentPath();
    void ShowStatusMessage(const std::wstring& message);
    void ShowPersistentStatusMessage(const std::wstring& message);
    void ScrollCanvas(int direction); // 0=up, 1=down, 2=left, 3=right
    std::wstring GetCurrentWorkingPath();
    void HandleMouseWheelScroll(int delta);
    void CleanupCanvasMemory(); // Cleanup edit control memory

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
    bool m_shouldCleanupCanvas; // Flag to trigger canvas memory cleanup
    std::wstring m_lastKnownPath;
    
    // Multithreading support
    std::thread m_generationThread;
    std::thread m_saveThread;
    std::atomic<bool> m_cancelGeneration;
    std::atomic<bool> m_isGenerating;
    std::atomic<bool> m_isSaving;
    std::mutex m_treeMutex;
    int m_animationStep;
    
    // GDI+ and custom drawing
    ULONG_PTR m_gdiplusToken;
    HWND m_hoveredButton;
    HWND m_pressedButton;
    
    // Animation for smooth hover effects
    std::map<HWND, float> m_buttonHoverAlpha;
    UINT_PTR m_animationTimer;
    
    // Dark theme brushes
    HBRUSH m_hBgBrush;
    HBRUSH m_hEditBrush;
    HBRUSH m_hStaticBrush;
    
    // Fonts
    HFONT m_hFont;
    HFONT m_hMonoFont;
    
    static const UINT_PTR STATUS_TIMER_ID = 1;
    static const UINT_PTR PATH_UPDATE_TIMER_ID = 2;
    static const UINT_PTR ANIMATION_TIMER_ID = 3;
    static const UINT_PTR PROGRESS_TIMER_ID = 4;
    
    static const UINT WM_TREE_COMPLETED = WM_USER + 100;
    static const UINT WM_TREE_ERROR = WM_USER + 101;
    static const UINT WM_SAVE_COMPLETED = WM_USER + 102;
    static const UINT WM_SAVE_ERROR = WM_USER + 103;

    static const int MIN_WIDTH = 600;
    static const int MIN_HEIGHT = 400;
};