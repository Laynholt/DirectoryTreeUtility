#include "Application.h"
#include "DirectoryTreeBuilder.h"
#include "SystemTray.h"
#include "GlobalHotkeys.h"
#include "FileExplorerIntegration.h"
#include "UiRenderer.h"
#include "TreeGenerationService.h"
#include "FileSaveService.h"
#include "resource.h"

#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <malloc.h>
#include <psapi.h>
#include <richedit.h>
#include <gdiplus.h>
#include <uxtheme.h>

#pragma comment(lib, "comctl32.lib")

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "uxtheme.lib")

using namespace Gdiplus;

const wchar_t* WINDOW_CLASS = L"DirectoryTreeUtilityClass";
const wchar_t* WINDOW_TITLE = L"Directory Tree Utility";
const wchar_t* INFO_WINDOW_CLASS = L"DirectoryTreeUtilityInfoWindowClass";
const wchar_t* APP_VERSION = L"1.3.1";
const UINT WM_ACTIVATE_INSTANCE = WM_USER + 200;

enum ControlIDs {
    ID_DEPTH_EDIT = 1001,
    ID_PATH_EDIT = 1002,
    ID_GENERATE_BTN = 1003,
    ID_COPY_BTN = 1004,
    ID_SAVE_BTN = 1005,
    ID_HELP_BTN = 1006,
    ID_TREE_CANVAS = 1007,
    ID_INFO_TEXT = 1008,
    ID_INFO_CLOSE = 1009,
    ID_EXPAND_SYMLINKS_CHECK = 1010,
    ID_MENU_HELP_HOTKEYS = 2001,
    ID_MENU_HELP_ABOUT = 2002,
    ID_MENU_CONTEXT_COPY = 2102
};

namespace {
struct InfoWindowState {
    Application* owner;
    int kind;
    HWND textControl;
    HWND closeButton;
    std::wstring text;
    HFONT font;
    HBRUSH editBrush;
    bool useRichText;
};

HMODULE g_richEditModule = nullptr;
constexpr size_t TREE_LARGE_CHARS_THRESHOLD = 400000;
constexpr size_t TREE_SHRINK_FACTOR = 4;
constexpr UINT_PTR TREE_CANVAS_SUBCLASS_ID = 1;
constexpr UINT_PTR DEPTH_EDIT_SUBCLASS_ID = 2;
constexpr UINT_PTR TEXT_CONTEXT_SUBCLASS_ID = 3;

const wchar_t* GetHelpMenuItemText(UINT itemId) {
    switch (itemId) {
    case ID_MENU_HELP_HOTKEYS:
        return L"Горячие клавиши";
    case ID_MENU_HELP_ABOUT:
        return L"О программе";
    case ID_MENU_CONTEXT_COPY:
        return L"Копировать";
    default:
        return nullptr;
    }
}

const wchar_t* GetMenuItemText(UINT itemId, ULONG_PTR itemData) {
    if (itemData != 0) {
        return reinterpret_cast<const wchar_t*>(itemData);
    }

    return GetHelpMenuItemText(itemId);
}

void ApplyHotkeysFormatting(HWND richEdit, const std::wstring& text) {
    // RichEdit stores line breaks as '\r', so normalize text for correct selection offsets.
    std::wstring richText;
    richText.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == L'\r' && (i + 1) < text.size() && text[i + 1] == L'\n') {
            richText.push_back(L'\r');
            ++i;
            continue;
        }
        richText.push_back(text[i]);
    }

    SetWindowText(richEdit, richText.c_str());
    SendMessage(richEdit, EM_SETBKGNDCOLOR, FALSE, RGB(45, 45, 45));

    CHARRANGE allRange = {0, -1};
    SendMessage(richEdit, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&allRange));

    PARAFORMAT2 paragraphFormat = {};
    paragraphFormat.cbSize = sizeof(paragraphFormat);
    paragraphFormat.dwMask = PFM_TABSTOPS;
    paragraphFormat.cTabCount = 1;
    paragraphFormat.rgxTabs[0] = 2600; // Twips: aligns descriptions after dash.
    SendMessage(richEdit, EM_SETPARAFORMAT, 0, reinterpret_cast<LPARAM>(&paragraphFormat));

    CHARFORMAT2W baseFormat = {};
    baseFormat.cbSize = sizeof(baseFormat);
    baseFormat.dwMask = CFM_BOLD | CFM_COLOR | CFM_FACE | CFM_SIZE;
    baseFormat.dwEffects = 0;
    baseFormat.crTextColor = RGB(255, 255, 255);
    baseFormat.yHeight = 220;
    wcscpy_s(baseFormat.szFaceName, L"Segoe UI");
    SendMessage(richEdit, EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&baseFormat));

    const CHARFORMAT2W boldFormat = []() {
        CHARFORMAT2W format = {};
        format.cbSize = sizeof(format);
        format.dwMask = CFM_BOLD;
        format.dwEffects = CFE_BOLD;
        return format;
    }();

    auto applyBoldRange = [&](const wchar_t* headerText) {
        const size_t startPos = richText.find(headerText);
        if (startPos == std::wstring::npos) {
            return;
        }

        CHARRANGE range = {
            static_cast<LONG>(startPos),
            static_cast<LONG>(startPos + wcslen(headerText))
        };
        SendMessage(richEdit, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&range));
        SendMessage(richEdit, EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&boldFormat));
    };

    applyBoldRange(L"Глобальные горячие клавиши:");
    applyBoldRange(L"Локальные горячие клавиши (в окне приложения):");

    CHARRANGE clearSelection = {-1, -1};
    SendMessage(richEdit, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&clearSelection));
}
} // namespace

Application::Application()
    : m_hInstance(nullptr)
    , m_hWnd(nullptr)
    , m_hDepthEdit(nullptr)
    , m_hPathEdit(nullptr)
    , m_hGenerateBtn(nullptr)
    , m_hCopyBtn(nullptr)
    , m_hSaveBtn(nullptr)
    , m_hHelpBtn(nullptr)
    , m_hExpandSymlinksCheck(nullptr)
    , m_hTreeCanvas(nullptr)
    , m_hStatusLabel(nullptr)
    , m_hHotkeysWindow(nullptr)
    , m_hAboutWindow(nullptr)
    , m_hMainMenu(nullptr)
    , m_previousTreeSizeBeforeBuild(0)
    , m_previousTreeCapacityBeforeBuild(0)
    , m_expandSymlinks(false)
    , m_currentDepth(1)
    , m_isMinimized(false)
    , m_isDefaultDepthValue(true)
    , m_hasGeneratedTree(false)
    , m_gdiplusToken(0)
    , m_hoveredButton(nullptr)
    , m_pressedButton(nullptr)
    , m_animationTimer(0)
    , m_hBgBrush(nullptr)
    , m_hEditBrush(nullptr)
    , m_hStaticBrush(nullptr)
    , m_hFont(nullptr)
    , m_hMonoFont(nullptr)
    , m_hInfoFont(nullptr)
    , m_isGenerating(false)
    , m_isSaving(false)
    , m_animationStep(0) {
    
    // Initialize dark theme brushes
    m_hBgBrush = CreateSolidBrush(RGB(26, 26, 26));      // --bg-color: #1a1a1a
    m_hEditBrush = CreateSolidBrush(RGB(45, 45, 45));    // --card-bg: #2d2d2d
    m_hStaticBrush = CreateSolidBrush(RGB(26, 26, 26));  // Same as background
}

Application::~Application() {
    // Ensure background thread is properly cleaned up
    CancelGeneration();
    if (m_fileSaveService) {
        m_fileSaveService->Cancel();
    }
}

bool Application::Initialize(HINSTANCE hInstance) {
    m_hInstance = hInstance;
    
    // Initialize GDI+
    GdiplusStartupInput gdiplusStartupInput;
    GdiplusStartup(&m_gdiplusToken, &gdiplusStartupInput, nullptr);
    
    // Initialize COM
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hr)) {
        MessageBox(nullptr, L"Ошибка инициализации COM", L"Отладка", MB_OK);
        return false;
    }
    
    // Initialize Common Controls
    INITCOMMONCONTROLSEX icex = {};
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_WIN95_CLASSES;
    InitCommonControlsEx(&icex);

    if (!g_richEditModule) {
        g_richEditModule = LoadLibrary(L"Msftedit.dll");
    }

    WNDCLASSEX wcex = {};
    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wcex.lpfnWndProc = WindowProc;
    wcex.cbClsExtra = 0;
    wcex.cbWndExtra = 0;
    wcex.hInstance = hInstance;
    wcex.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_MAIN_ICON));
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    // Use dark theme background color
    wcex.hbrBackground = m_hBgBrush; // --bg-color: #1a1a1a
    wcex.lpszMenuName = nullptr;
    wcex.lpszClassName = WINDOW_CLASS;
    wcex.hIconSm = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_MAIN_ICON));

    if (!RegisterClassEx(&wcex)) {
        MessageBox(nullptr, L"Ошибка регистрации класса окна", L"Отладка", MB_OK);
        return false;
    }

    // Get screen dimensions for centering
    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);
    int windowWidth = 800;
    int windowHeight = 600;
    int x = (screenWidth - windowWidth) / 2;
    int y = (screenHeight - windowHeight) / 2;

    m_hWnd = CreateWindowEx(
        0,
        WINDOW_CLASS,
        WINDOW_TITLE,
        WS_OVERLAPPEDWINDOW,
        x, y,
        windowWidth, windowHeight,
        nullptr,
        nullptr,
        hInstance,
        this
    );

    if (!m_hWnd) {
        DWORD error = GetLastError();
        wchar_t errorMsg[256];
        swprintf_s(errorMsg, L"Ошибка создания главного окна. Код ошибки: %lu", error);
        MessageBox(nullptr, errorMsg, L"Отладка", MB_OK);
        return false;
    }

    CreateMainMenu();
    CreateControls();
    RECT initialClientRect = {};
    GetClientRect(m_hWnd, &initialClientRect);
    OnResize(initialClientRect.right - initialClientRect.left, initialClientRect.bottom - initialClientRect.top);
    if (!RegisterInfoWindowClass()) {
        MessageBox(nullptr, L"Ошибка регистрации класса информационного окна", L"Отладка", MB_OK);
        return false;
    }
    
    m_systemTray = std::make_unique<SystemTray>(this);
    m_globalHotkeys = std::make_unique<GlobalHotkeys>(this);
    m_treeGenerationService = std::make_unique<TreeGenerationService>();
    m_fileSaveService = std::make_unique<FileSaveService>();
    
    if (!m_systemTray->Initialize()) {
        MessageBox(nullptr, L"Ошибка инициализации системного трея", L"Отладка", MB_OK);
        return false;
    }
    
    if (!m_globalHotkeys->Initialize()) {
        MessageBox(nullptr, L"Ошибка инициализации горячих клавиш", L"Отладка", MB_OK);
        return false;
    }

    m_systemTray->AddToTray();
    
    UpdateCurrentPath();

    // Start timer for dynamic path updating.
    SetTimer(m_hWnd, PATH_UPDATE_TIMER_ID, 500, nullptr);

    return true;
}

void Application::CreateControls() {
    // Create modern font matching the HTML example
    m_hFont = CreateFont(
        -15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI"
    );
    
    // Create labels positioned inside input card without extra space for title
    HWND hDepthLabel = CreateWindowEx(0, L"STATIC", L"Глубина дерева:",
                  WS_VISIBLE | WS_CHILD,
                  24, 18, 140, 20, m_hWnd, nullptr, m_hInstance, nullptr);
    UNREFERENCED_PARAMETER(hDepthLabel);
    
    HWND hPathLabel = CreateWindowEx(0, L"STATIC", L"Текущий путь:",
                  WS_VISIBLE | WS_CHILD,
                  24, 48, 140, 20, m_hWnd, nullptr, m_hInstance, nullptr);
    UNREFERENCED_PARAMETER(hPathLabel);

    // Enhanced depth input positioned inside card
    m_hDepthEdit = CreateWindowEx(0, L"EDIT", L"1",
                                 WS_VISIBLE | WS_CHILD | ES_NUMBER,
                                 174, 16, 80, 24, m_hWnd, reinterpret_cast<HMENU>(ID_DEPTH_EDIT), m_hInstance, nullptr);

    // Enhanced path display inside card
    m_hPathEdit = CreateWindowEx(0, L"EDIT", L"",
                                WS_VISIBLE | WS_CHILD | ES_READONLY,
                                174, 46, 480, 24, m_hWnd, reinterpret_cast<HMENU>(ID_PATH_EDIT), m_hInstance, nullptr);

    // Buttons positioned inside button card - larger size and vertically centered
    m_hGenerateBtn = CreateWindowEx(0, L"BUTTON", L"Построить дерево",
                                   WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
                                   100, 104, 170, 32, m_hWnd, reinterpret_cast<HMENU>(ID_GENERATE_BTN), m_hInstance, nullptr);

    m_hCopyBtn = CreateWindowEx(0, L"BUTTON", L"Копировать",
                               WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
                               280, 104, 150, 32, m_hWnd, reinterpret_cast<HMENU>(ID_COPY_BTN), m_hInstance, nullptr);

    m_hSaveBtn = CreateWindowEx(0, L"BUTTON", L"Сохранить",
                               WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
                               440, 104, 150, 32, m_hWnd, reinterpret_cast<HMENU>(ID_SAVE_BTN), m_hInstance, nullptr);

    m_hHelpBtn = CreateWindowEx(0, L"BUTTON", L"Справка",
                               WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
                               658, 16, 106, 24, m_hWnd, reinterpret_cast<HMENU>(ID_HELP_BTN), m_hInstance, nullptr);

    m_hExpandSymlinksCheck = CreateWindowEx(
        0,
        L"BUTTON",
        L"Раскрывать симлинки",
        WS_VISIBLE | WS_CHILD | BS_OWNERDRAW | WS_TABSTOP,
        300, 18, 220, 24,
        m_hWnd,
        reinterpret_cast<HMENU>(ID_EXPAND_SYMLINKS_CHECK),
        m_hInstance,
        nullptr
    );

    // Tree canvas positioned inside tree card - scrollbars appear automatically when needed
    m_hTreeCanvas = CreateWindowEx(0, L"EDIT", L"",
                                  WS_VISIBLE | WS_CHILD |
                                  ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | ES_AUTOHSCROLL,
                                  24, 176, 740, 300, m_hWnd, reinterpret_cast<HMENU>(ID_TREE_CANVAS), m_hInstance, nullptr);
    
    // Subclass the tree canvas to handle mouse wheel
    SetWindowSubclass(m_hTreeCanvas, TreeCanvasSubclassProc, TREE_CANVAS_SUBCLASS_ID, reinterpret_cast<DWORD_PTR>(this));
    
    // Subclass the depth edit to handle minus key
    SetWindowSubclass(m_hDepthEdit, DepthEditSubclassProc, DEPTH_EDIT_SUBCLASS_ID, reinterpret_cast<DWORD_PTR>(this));
    SetWindowSubclass(m_hPathEdit, TextEditSubclassProc, TEXT_CONTEXT_SUBCLASS_ID, reinterpret_cast<DWORD_PTR>(this));

    // Status label positioned inside status card
    m_hStatusLabel = CreateWindowEx(0, L"STATIC", L"Готово",
                                   WS_VISIBLE | WS_CHILD,
                                   24, 488, 740, 20, m_hWnd, nullptr, m_hInstance, nullptr);

    // Apply the modern font to all controls
    SendMessage(m_hDepthEdit, WM_SETFONT, reinterpret_cast<WPARAM>(m_hFont), MAKELPARAM(FALSE, 0));
    SendMessage(m_hPathEdit, WM_SETFONT, reinterpret_cast<WPARAM>(m_hFont), MAKELPARAM(FALSE, 0));
    SendMessage(m_hHelpBtn, WM_SETFONT, reinterpret_cast<WPARAM>(m_hFont), MAKELPARAM(FALSE, 0));
    SendMessage(m_hExpandSymlinksCheck, WM_SETFONT, reinterpret_cast<WPARAM>(m_hFont), MAKELPARAM(FALSE, 0));
    SendMessage(m_hStatusLabel, WM_SETFONT, reinterpret_cast<WPARAM>(m_hFont), MAKELPARAM(FALSE, 0));
    
    // Apply font to static labels too
    EnumChildWindows(m_hWnd, [](HWND hwndChild, LPARAM lParam) -> BOOL {
        wchar_t className[256];
        GetClassName(hwndChild, className, 256);
        if (wcscmp(className, L"Static") == 0) {
            SendMessage(hwndChild, WM_SETFONT, lParam, MAKELPARAM(FALSE, 0));
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(m_hFont));

    // Enhanced monospace font for tree display
    m_hMonoFont = CreateFont(
        -15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas"
    );
    SendMessage(m_hTreeCanvas, WM_SETFONT, reinterpret_cast<WPARAM>(m_hMonoFont), MAKELPARAM(FALSE, 0));
    SendMessage(m_hTreeCanvas, EM_SETUNDOLIMIT, 0, 0);

    m_hInfoFont = CreateFont(
        -15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI"
    );
    
    // Initialize button animation values
    m_buttonHoverAlpha[m_hGenerateBtn] = 0.0f;
    m_buttonHoverAlpha[m_hCopyBtn] = 0.0f;
    m_buttonHoverAlpha[m_hSaveBtn] = 0.0f;
    m_buttonHoverAlpha[m_hHelpBtn] = 0.0f;
}

void Application::CreateMainMenu() {
    m_hMainMenu = CreatePopupMenu();
    if (!m_hMainMenu) {
        return;
    }

    AppendMenu(m_hMainMenu, MF_OWNERDRAW, ID_MENU_HELP_HOTKEYS, GetMenuItemText(ID_MENU_HELP_HOTKEYS, 0));
    AppendMenu(m_hMainMenu, MF_OWNERDRAW, ID_MENU_HELP_ABOUT, GetMenuItemText(ID_MENU_HELP_ABOUT, 0));

    MENUINFO popupMenuInfo = {};
    popupMenuInfo.cbSize = sizeof(MENUINFO);
    popupMenuInfo.fMask = MIM_BACKGROUND;
    popupMenuInfo.hbrBack = m_hEditBrush;
    SetMenuInfo(m_hMainMenu, &popupMenuInfo);
}

bool Application::RegisterInfoWindowClass() {
    WNDCLASSEX wcex = {};
    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = InfoWindowProc;
    wcex.hInstance = m_hInstance;
    wcex.hIcon = LoadIcon(m_hInstance, MAKEINTRESOURCE(IDI_MAIN_ICON));
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground = nullptr;
    wcex.lpszClassName = INFO_WINDOW_CLASS;
    wcex.hIconSm = LoadIcon(m_hInstance, MAKEINTRESOURCE(IDI_MAIN_ICON));

    if (!RegisterClassEx(&wcex)) {
        return GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
    }

    return true;
}

int Application::Run() {
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        // Check if this is a key message for our window or its children
        if ((msg.message == WM_KEYDOWN || msg.message == WM_CHAR) && 
            (msg.hwnd == m_hWnd || IsChild(m_hWnd, msg.hwnd))) {
            
            // Handle special keys globally
            if (msg.message == WM_KEYDOWN) {
                if (msg.wParam == VK_ESCAPE) {
                    SetFocus(m_hWnd);
                    continue;
                }
                else if (msg.wParam == VK_RETURN) {
                    GenerateTree();
                    continue;
                }
                else if (msg.wParam == 'C' && (GetKeyState(VK_CONTROL) & 0x8000)) {
                    CopyToClipboard();
                    continue;
                }
                else if (msg.wParam == 'S' && (GetKeyState(VK_CONTROL) & 0x8000)) {
                    SaveToFile();
                    continue;
                }
                else if (msg.wParam >= '0' && msg.wParam <= '9' && msg.hwnd != m_hDepthEdit) {
                    // Number input - redirect to depth edit
                    SetFocus(m_hDepthEdit);
                    // Send the character to the edit control
                    SendMessage(m_hDepthEdit, WM_CHAR, msg.wParam, 0);
                    continue;
                }
            }
        }
        
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return static_cast<int>(msg.wParam);
}

void Application::Shutdown() {
    // Remove subclassing
    if (m_hTreeCanvas) {
        RemoveWindowSubclass(m_hTreeCanvas, TreeCanvasSubclassProc, TREE_CANVAS_SUBCLASS_ID);
    }
    if (m_hDepthEdit) {
        RemoveWindowSubclass(m_hDepthEdit, DepthEditSubclassProc, DEPTH_EDIT_SUBCLASS_ID);
    }
    if (m_hPathEdit) {
        RemoveWindowSubclass(m_hPathEdit, TextEditSubclassProc, TEXT_CONTEXT_SUBCLASS_ID);
    }
    
    if (m_globalHotkeys) {
        m_globalHotkeys->Cleanup();
        m_globalHotkeys.reset();
    }
    
    if (m_systemTray) {
        m_systemTray->Cleanup();
        m_systemTray.reset();
    }
    
    if (m_treeGenerationService) {
        m_treeGenerationService->Cancel();
        m_treeGenerationService.reset();
    }

    if (m_fileSaveService) {
        m_fileSaveService->Cancel();
        m_fileSaveService.reset();
    }

    if (m_hHotkeysWindow && IsWindow(m_hHotkeysWindow)) {
        DestroyWindow(m_hHotkeysWindow);
        m_hHotkeysWindow = nullptr;
    }
    if (m_hAboutWindow && IsWindow(m_hAboutWindow)) {
        DestroyWindow(m_hAboutWindow);
        m_hAboutWindow = nullptr;
    }
    
    // Clean up brushes
    if (m_hBgBrush) DeleteObject(m_hBgBrush);
    if (m_hEditBrush) DeleteObject(m_hEditBrush);
    if (m_hStaticBrush) DeleteObject(m_hStaticBrush);
    
    // Clean up fonts
    if (m_hFont) DeleteObject(m_hFont);
    if (m_hMonoFont) DeleteObject(m_hMonoFont);
    if (m_hInfoFont) DeleteObject(m_hInfoFont);

    if (m_hMainMenu) {
        DestroyMenu(m_hMainMenu);
        m_hMainMenu = nullptr;
    }
    
    // Stop animation timer if running
    if (m_animationTimer) {
        KillTimer(m_hWnd, ANIMATION_TIMER_ID);
        m_animationTimer = 0;
    }
    
    // Shutdown GDI+
    if (m_gdiplusToken) {
        GdiplusShutdown(m_gdiplusToken);
    }
    
    // Uninitialize COM
    CoUninitialize();

    if (g_richEditModule) {
        FreeLibrary(g_richEditModule);
        g_richEditModule = nullptr;
    }
}

void Application::ShowWindow(bool show) {
    if (show) {
        if (IsIconic(m_hWnd)) {
            ::ShowWindow(m_hWnd, SW_RESTORE);  // Restore from minimized state
        } else {
            ::ShowWindow(m_hWnd, SW_SHOW);     // Normal show
        }
        
        ::SetForegroundWindow(m_hWnd);
        m_isMinimized = false;
    } else {
        ::ShowWindow(m_hWnd, SW_HIDE);
        m_isMinimized = true;
    }
}

void Application::ToggleVisibility() {
    if (m_isMinimized || !IsWindowVisible(m_hWnd) || IsIconic(m_hWnd)) {
        // Window is hidden or minimized - show it
        ShowWindow(true);
    } else if (GetForegroundWindow() == m_hWnd) {
        // Window is visible and active - hide it
        ShowWindow(false);
    } else {
        // Window is visible but not active - bring to front
        SetForegroundWindow(m_hWnd);
        BringWindowToTop(m_hWnd);
        SetActiveWindow(m_hWnd);
    }
}

LRESULT CALLBACK Application::WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    Application* pApp = nullptr;
    
    if (message == WM_NCCREATE) {
        CREATESTRUCT* pCreate = reinterpret_cast<CREATESTRUCT*>(lParam);
        pApp = static_cast<Application*>(pCreate->lpCreateParams);
        SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pApp));
        return DefWindowProc(hWnd, message, wParam, lParam);
    } else {
        pApp = reinterpret_cast<Application*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));
    }

    if (pApp) {
        return pApp->HandleMessage(message, wParam, lParam);
    }

    return DefWindowProc(hWnd, message, wParam, lParam);
}

LRESULT Application::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_COMMAND:
        {
            const UINT commandId = LOWORD(wParam);
            const UINT notificationCode = HIWORD(wParam);

            if (notificationCode == EN_SETFOCUS && reinterpret_cast<HWND>(lParam) == m_hDepthEdit) {
                // When depth edit gets focus, select all text for easy replacement
                SendMessage(m_hDepthEdit, EM_SETSEL, 0, -1);
            }
            if (commandId == ID_EXPAND_SYMLINKS_CHECK && notificationCode != BN_CLICKED) {
                return 0;
            }

            OnCommand(commandId);
        }
        break;

    case WM_MEASUREITEM:
        {
            MEASUREITEMSTRUCT* mis = reinterpret_cast<MEASUREITEMSTRUCT*>(lParam);
            if (mis && mis->CtlType == ODT_MENU) {
                const wchar_t* text = GetMenuItemText(mis->itemID, mis->itemData);
                if (text) {
                    HDC hdc = GetDC(m_hWnd);
                    if (!hdc) {
                        return FALSE;
                    }

                    HFONT hMenuFont = m_hFont ? m_hFont : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
                    HFONT hOldFont = static_cast<HFONT>(SelectObject(hdc, hMenuFont));

                    SIZE textSize = {};
                    GetTextExtentPoint32(hdc, text, lstrlenW(text), &textSize);

                    mis->itemWidth = textSize.cx + 38;
                    mis->itemHeight = (textSize.cy + 10 > 26) ? (textSize.cy + 10) : 26;

                    SelectObject(hdc, hOldFont);
                    ReleaseDC(m_hWnd, hdc);
                    return TRUE;
                }
            }
        }
        break;

    case WM_DRAWITEM:
        {
            DRAWITEMSTRUCT* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
            if (dis->CtlType == ODT_BUTTON) {
                if (dis->CtlID == ID_EXPAND_SYMLINKS_CHECK) {
                    wchar_t checkboxText[256];
                    GetWindowText(dis->hwndItem, checkboxText, 256);
                    UiRenderer::DrawCustomCheckbox(
                        dis->hDC,
                        dis->hwndItem,
                        checkboxText,
                        m_expandSymlinks,
                        (dis->itemState & ODS_HOTLIGHT) != 0,
                        (dis->itemState & ODS_SELECTED) != 0,
                        (dis->itemState & ODS_DISABLED) == 0,
                        (dis->itemState & ODS_FOCUS) != 0
                    );
                    return TRUE;
                }

                std::wstring buttonText;
                wchar_t text[256];
                GetWindowText(dis->hwndItem, text, 256);
                buttonText = text;
                
                bool isPressed = (m_pressedButton == dis->hwndItem) || (dis->itemState & ODS_SELECTED);
                
                UiRenderer::DrawCustomButton(dis->hDC, dis->hwndItem, buttonText, isPressed, m_buttonHoverAlpha[dis->hwndItem]);
                return TRUE;
            }

            if (dis->CtlType == ODT_MENU) {
                const wchar_t* text = GetMenuItemText(dis->itemID, dis->itemData);
                if (text) {
                    const bool isSelected = (dis->itemState & ODS_SELECTED) != 0;
                    const bool isDisabled = (dis->itemState & ODS_DISABLED) != 0;

                    HBRUSH hBgBrush = CreateSolidBrush(isSelected ? RGB(68, 68, 68) : RGB(45, 45, 45));
                    FillRect(dis->hDC, &dis->rcItem, hBgBrush);
                    DeleteObject(hBgBrush);

                    if (isSelected) {
                        HPEN hPen = CreatePen(PS_SOLID, 1, RGB(85, 85, 85));
                        HPEN hOldPen = static_cast<HPEN>(SelectObject(dis->hDC, hPen));
                        HBRUSH hOldBrush = static_cast<HBRUSH>(SelectObject(dis->hDC, GetStockObject(NULL_BRUSH)));
                        Rectangle(dis->hDC, dis->rcItem.left, dis->rcItem.top, dis->rcItem.right, dis->rcItem.bottom);
                        SelectObject(dis->hDC, hOldBrush);
                        SelectObject(dis->hDC, hOldPen);
                        DeleteObject(hPen);
                    }

                    HFONT hMenuFont = m_hFont ? m_hFont : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
                    HFONT hOldFont = static_cast<HFONT>(SelectObject(dis->hDC, hMenuFont));
                    SetBkMode(dis->hDC, TRANSPARENT);
                    SetTextColor(dis->hDC, isDisabled ? RGB(150, 150, 150) : RGB(255, 255, 255));

                    RECT textRect = dis->rcItem;
                    textRect.left += 14;
                    DrawText(dis->hDC, text, -1, &textRect, DT_SINGLELINE | DT_VCENTER | DT_LEFT);

                    SelectObject(dis->hDC, hOldFont);
                    return TRUE;
                }
            }
        }
        break;

    case WM_INITMENUPOPUP:
        {
            HMENU popupMenu = reinterpret_cast<HMENU>(wParam);
            if (popupMenu) {
                MENUINFO popupMenuInfo = {};
                popupMenuInfo.cbSize = sizeof(MENUINFO);
                popupMenuInfo.fMask = MIM_BACKGROUND;
                popupMenuInfo.hbrBack = m_hEditBrush;
                SetMenuInfo(popupMenu, &popupMenuInfo);
            }
        }
        break;


    case WM_LBUTTONDOWN:
        OnLButtonDown(lParam);
        break;

    case WM_LBUTTONUP:
        OnLButtonUp(lParam);
        break;

    case WM_SETCURSOR:
        {
            // Handle cursor only for client area
            if (LOWORD(lParam) == HTCLIENT) {
                POINT pt;
                GetCursorPos(&pt);
                ScreenToClient(m_hWnd, &pt);
                
                HWND previousHovered = m_hoveredButton;
                m_hoveredButton = nullptr;
                
                // Check if mouse is over any button
                if (IsPointInButton(m_hGenerateBtn, pt)) {
                    m_hoveredButton = m_hGenerateBtn;
                } else if (IsPointInButton(m_hCopyBtn, pt)) {
                    m_hoveredButton = m_hCopyBtn;
                } else if (IsPointInButton(m_hSaveBtn, pt)) {
                    m_hoveredButton = m_hSaveBtn;
                } else if (IsPointInButton(m_hHelpBtn, pt)) {
                    m_hoveredButton = m_hHelpBtn;
                }
                
                // Start animation if hover state changed
                if (previousHovered != m_hoveredButton) {
                    // Start fast animation timer
                    if (!m_animationTimer) {
                        m_animationTimer = SetTimer(m_hWnd, ANIMATION_TIMER_ID, 8, nullptr); // ~120 FPS for smoother fast animation
                    }
                }
                
                // Set appropriate cursor based on what's under the mouse
                HWND childWnd = ChildWindowFromPoint(m_hWnd, pt);
                if (childWnd && childWnd != m_hWnd) {
                    // Check if it's an edit control
                    wchar_t className[256];
                    GetClassName(childWnd, className, 256);
                    if (wcscmp(className, L"Edit") == 0) {
                        // Check if it's a read-only edit control
                        LONG style = GetWindowLong(childWnd, GWL_STYLE);
                        if (style & ES_READONLY) {
                            // Read-only edit - show arrow cursor
                            SetCursor(LoadCursor(nullptr, IDC_ARROW));
                        } else {
                            // Editable field - show I-beam cursor
                            SetCursor(LoadCursor(nullptr, IDC_IBEAM));
                        }
                    } else {
                        // Over other controls - show arrow
                        SetCursor(LoadCursor(nullptr, IDC_ARROW));
                    }
                } else {
                    // Over main window background - show arrow
                    SetCursor(LoadCursor(nullptr, IDC_ARROW));
                }
                
                return TRUE; // We handled the cursor
            }
            // Let Windows handle non-client area cursors
            return DefWindowProc(m_hWnd, message, wParam, lParam);
        }
        break;

    case WM_MOUSELEAVE:
        {
            // Mouse left window - clear hover state
            HWND previousHovered = m_hoveredButton;
            m_hoveredButton = nullptr;
            
            if (previousHovered) {
                InvalidateButton(previousHovered);
            }
        }
        break;

    case WM_CTLCOLORSTATIC:
        {
            // Dark theme for static text (labels) - same background as cards
            HDC hdcStatic = reinterpret_cast<HDC>(wParam);
            SetTextColor(hdcStatic, RGB(255, 255, 255));      // White text
            SetBkColor(hdcStatic, RGB(45, 45, 45));           // Card background color
            return reinterpret_cast<INT_PTR>(m_hEditBrush);  // Use same brush as cards
        }
        
    case WM_CTLCOLOREDIT:
        {
            // Dark theme for edit controls - --card-bg: #2d2d2d with --text-color: #ffffff
            HDC hdcEdit = reinterpret_cast<HDC>(wParam);
            SetTextColor(hdcEdit, RGB(255, 255, 255));        // White text
            SetBkColor(hdcEdit, RGB(45, 45, 45));             // Dark card background
            return reinterpret_cast<INT_PTR>(m_hEditBrush);
        }

    case WM_SIZE:
        OnResize(LOWORD(lParam), HIWORD(lParam));
        break;

    case WM_PAINT:
        OnPaint();
        // Draw dark borders around edit controls
        UiRenderer::DrawEditBorder(m_hWnd, m_hDepthEdit);
        UiRenderer::DrawEditBorder(m_hWnd, m_hPathEdit);
        UiRenderer::DrawEditBorder(m_hWnd, m_hTreeCanvas);
        break;

    case WM_KEYDOWN:
        OnKeyDown(wParam, lParam);
        break;
        
    case WM_MOUSEWHEEL:
        {
            // Get mouse position and check if it's over the tree canvas
            POINT pt;
            pt.x = GET_X_LPARAM(lParam);
            pt.y = GET_Y_LPARAM(lParam);
            ScreenToClient(m_hWnd, &pt);
            
            RECT canvasRect;
            GetWindowRect(m_hTreeCanvas, &canvasRect);
            ScreenToClient(m_hWnd, (LPPOINT)&canvasRect.left);
            ScreenToClient(m_hWnd, (LPPOINT)&canvasRect.right);
            
            if (PtInRect(&canvasRect, pt)) {
                // Mouse is over tree canvas - forward the wheel message
                int delta = GET_WHEEL_DELTA_WPARAM(wParam);
                HandleMouseWheelScroll(delta);
                return 0;
            }
        }
        break;

    case WM_GETMINMAXINFO:
        {
            MINMAXINFO* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
            mmi->ptMinTrackSize.x = MIN_WIDTH;
            mmi->ptMinTrackSize.y = MIN_HEIGHT;
        }
        break;

    case WM_CLOSE:
        ShowWindow(false);
        return 0;

    case WM_TIMER:
        if (wParam == STATUS_TIMER_ID) {
            // Don't reset status if we're still generating or saving
            if (!m_isGenerating.load() && !m_isSaving.load()) {
                SetWindowText(m_hStatusLabel, L"Готово");
            }
            KillTimer(m_hWnd, STATUS_TIMER_ID);
        }
        else if (wParam == PATH_UPDATE_TIMER_ID) {
            // Poll active Explorer path directly without fallback to CWD.
            // This keeps the last valid Explorer path when Explorer is not focused.
            std::wstring explorerPath = FileExplorerIntegration::GetActiveExplorerPath();
            if (!explorerPath.empty() && explorerPath != m_lastKnownPath) {
                m_lastKnownPath = explorerPath;
                SetWindowText(m_hPathEdit, explorerPath.c_str());
            }
        }
        else if (wParam == PROGRESS_TIMER_ID) {
            UpdateProgressAnimation();
        }
        else if (wParam == ANIMATION_TIMER_ID) {
            // Only redraw buttons that actually changed
            for (auto& pair : m_buttonHoverAlpha) {
                HWND button = pair.first;
                float& alpha = pair.second;
                float targetAlpha = (button == m_hoveredButton) ? 1.0f : 0.0f;
                
                // Only update and redraw if there's a change
                if (alpha != targetAlpha) {
                    alpha = targetAlpha;
                    // Direct redraw without invalidation to avoid clearing
                    RedrawButtonDirect(button);
                }
            }
            
            // Stop timer immediately
            KillTimer(m_hWnd, ANIMATION_TIMER_ID);
            m_animationTimer = 0;
        }
        break;

    case WM_NCCALCSIZE:
        // Let Windows handle the default calculation
        return DefWindowProc(m_hWnd, message, wParam, lParam);

    case WM_DESTROY:
        KillTimer(m_hWnd, STATUS_TIMER_ID);
        KillTimer(m_hWnd, PATH_UPDATE_TIMER_ID);
        KillTimer(m_hWnd, PROGRESS_TIMER_ID);
        CancelGeneration(); // Ensure background thread is stopped
        if (m_fileSaveService) {
            m_fileSaveService->Cancel();
        }
        PostQuitMessage(0);
        break;

    case WM_TREE_COMPLETED:
        {
            std::wstring* completedResult = reinterpret_cast<std::wstring*>(lParam);
            if (completedResult) {
                OnTreeGenerationCompleted(*completedResult);
                delete completedResult;
            } else {
                OnTreeGenerationError(L"Ошибка: результат построения дерева не получен");
            }
        }
        break;
        
    case WM_TREE_ERROR:
        {
            std::wstring* errorMsg = reinterpret_cast<std::wstring*>(lParam);
            OnTreeGenerationError(*errorMsg);
            delete errorMsg;
        }
        break;

    case WM_SAVE_COMPLETED:
        OnSaveCompleted();
        break;
        
    case WM_SAVE_ERROR:
        {
            std::wstring* errorMsg = reinterpret_cast<std::wstring*>(lParam);
            OnSaveError(*errorMsg);
            delete errorMsg;
        }
        break;

    case WM_ACTIVATE_INSTANCE:
        // Another instance tried to start - show this window properly
        ShowWindow(true);
        break;

    default:
        return DefWindowProc(m_hWnd, message, wParam, lParam);
    }
    return 0;
}

void Application::OnResize(int width, int height) {
    if (width > 0 && height > 0) {
        // Keep help button integrated into the top card.
        const int helpButtonWidth = 106;
        const int helpButtonHeight = 24;
        const int helpButtonX = width - 24 - helpButtonWidth;
        MoveWindow(m_hHelpBtn, helpButtonX, 16, helpButtonWidth, helpButtonHeight, TRUE);

        // Resize path edit inside input card and keep spacing before help button.
        const int pathX = 174;
        const int pathRight = helpButtonX - 16;
        const int pathWidth = (pathRight > pathX) ? (pathRight - pathX) : 120;
        MoveWindow(m_hPathEdit, pathX, 46, pathWidth, 24, TRUE);

        const int depthEditRight = 174 + 80;
        const int checkboxMinLeft = depthEditRight + 16;
        const int checkboxMaxRight = helpButtonX - 16;
        int checkboxWidth = 220;
        if (m_hExpandSymlinksCheck && IsWindow(m_hExpandSymlinksCheck)) {
            wchar_t checkboxText[128] = {};
            GetWindowText(m_hExpandSymlinksCheck, checkboxText, 128);

            HDC hdc = GetDC(m_hWnd);
            if (hdc) {
                HFONT oldFont = nullptr;
                if (m_hFont) {
                    oldFont = static_cast<HFONT>(SelectObject(hdc, m_hFont));
                }

                SIZE textSize = {};
                GetTextExtentPoint32(hdc, checkboxText, lstrlenW(checkboxText), &textSize);
                checkboxWidth = textSize.cx + 42; // checkbox square + gaps + right padding

                if (oldFont) {
                    SelectObject(hdc, oldFont);
                }
                ReleaseDC(m_hWnd, hdc);
            }
        }

        const int availableCheckboxWidth = checkboxMaxRight - checkboxMinLeft;
        if (checkboxWidth > availableCheckboxWidth) {
            checkboxWidth = availableCheckboxWidth;
        }
        int checkboxX = (width - checkboxWidth) / 2;
        if (checkboxX < checkboxMinLeft) {
            checkboxX = checkboxMinLeft;
        }
        if (checkboxX + checkboxWidth > checkboxMaxRight) {
            checkboxX = checkboxMaxRight - checkboxWidth;
        }
        if (checkboxX < checkboxMinLeft) {
            checkboxX = checkboxMinLeft;
        }
        MoveWindow(m_hExpandSymlinksCheck, checkboxX, 16, checkboxWidth, 24, TRUE);
        
        // Center buttons in the button card with larger sizes
        int totalButtonWidth = 170 + 150 + 150 + 20; // buttons + gaps
        int startX = (width - totalButtonWidth) / 2;
        MoveWindow(m_hGenerateBtn, startX, 104, 170, 32, TRUE);
        MoveWindow(m_hCopyBtn, startX + 180, 104, 150, 32, TRUE);
        MoveWindow(m_hSaveBtn, startX + 340, 104, 150, 32, TRUE);
        
        // Resize tree canvas inside tree card
        MoveWindow(m_hTreeCanvas, 24, 176, width - 48, height - 248, TRUE);
        
        // Position status label inside status card
        MoveWindow(m_hStatusLabel, 24, height - 40, width - 48, 20, TRUE);
        
        // Force redraw to update cards
        InvalidateRect(m_hWnd, nullptr, TRUE);
    }
}

void Application::OnPaint() {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(m_hWnd, &ps);
    
    RECT clientRect;
    GetClientRect(m_hWnd, &clientRect);
    
    // Draw main background
    UiRenderer::DrawBackground(hdc, clientRect);
    
    // Draw input controls card without title
    RECT inputCard = {8, 8, clientRect.right - 8, 84};
    UiRenderer::DrawCard(hdc, inputCard);
    
    // Draw buttons card without title
    RECT buttonCard = {8, 92, clientRect.right - 8, 140};
    UiRenderer::DrawCard(hdc, buttonCard);
    
    // Draw tree display card - main content area
    RECT treeCard = {8, 148, clientRect.right - 8, clientRect.bottom - 60};
    UiRenderer::DrawCard(hdc, treeCard, L"Дерево каталогов");
    
    // Draw status card
    RECT statusCard = {8, clientRect.bottom - 52, clientRect.right - 8, clientRect.bottom - 8};
    UiRenderer::DrawCard(hdc, statusCard);
    
    EndPaint(m_hWnd, &ps);
}

void Application::OnCommand(int commandId) {
    switch (commandId) {
    case ID_HELP_BTN:
        ShowHelpMenu();
        break;
    case ID_GENERATE_BTN:
        GenerateTree();
        break;
    case ID_COPY_BTN:
        CopyToClipboard();
        break;
    case ID_SAVE_BTN:
        SaveToFile();
        break;
    case ID_EXPAND_SYMLINKS_CHECK:
        m_expandSymlinks = !m_expandSymlinks;
        InvalidateRect(m_hExpandSymlinksCheck, nullptr, FALSE);
        UpdateWindow(m_hExpandSymlinksCheck);
        ShowStatusMessage(m_expandSymlinks ? L"Раскрытие симлинков включено" : L"Раскрытие симлинков выключено");
        break;
    case ID_MENU_HELP_HOTKEYS:
        ShowHotkeysWindow();
        break;
    case ID_MENU_HELP_ABOUT:
        ShowAboutWindow();
        break;
    }
}

void Application::OnKeyDown(WPARAM key, LPARAM) {
    HWND hFocused = GetFocus();

    switch (key) {
    case VK_BACK:
        if (hFocused != m_hDepthEdit) {
            SetFocus(m_hDepthEdit);
        }
        // Send message to depth edit subclass for processing
        SendMessage(m_hDepthEdit, WM_CHAR, VK_BACK, 0);
        break;
    case VK_OEM_MINUS:
        // Send message to depth edit subclass for processing
        SendMessage(m_hDepthEdit, WM_CHAR, VK_OEM_MINUS, 0);
        break;
    case VK_UP:
        ScrollCanvas(0);
        break;
    case VK_DOWN:
        ScrollCanvas(1);
        break;
    case VK_LEFT:
        ScrollCanvas(2);
        break;
    case VK_RIGHT:
        ScrollCanvas(3);
        break;
    default:
        break;
    }
}

void Application::CompactTreeBuffersForNextBuild(int nextDepth) {
    UNREFERENCED_PARAMETER(nextDepth);

    const size_t previousSize = m_treeContent.size();
    const size_t previousCapacity = m_treeContent.capacity();
    const bool hadLargeStorage = previousCapacity > TREE_LARGE_CHARS_THRESHOLD;
    const bool shouldShrinkTreeStorage = hadLargeStorage || previousSize > TREE_LARGE_CHARS_THRESHOLD;

    m_treeContent.clear();
    if (shouldShrinkTreeStorage) {
        std::wstring().swap(m_treeContent);
    }

    const bool shouldRecreateCanvas = previousSize > TREE_LARGE_CHARS_THRESHOLD;
    if (shouldRecreateCanvas) {
        RecreateTreeCanvasControl();
    } else if (m_hTreeCanvas && IsWindow(m_hTreeCanvas)) {
        SetWindowText(m_hTreeCanvas, L"");
        SendMessage(m_hTreeCanvas, EM_EMPTYUNDOBUFFER, 0, 0);
    }

    if (shouldShrinkTreeStorage || shouldRecreateCanvas) {
        TrimProcessMemoryUsage();
    }
}

void Application::RecreateTreeCanvasControl() {
    if (!m_hTreeCanvas || !IsWindow(m_hTreeCanvas)) {
        return;
    }

    RECT canvasRect = {};
    GetWindowRect(m_hTreeCanvas, &canvasRect);
    ScreenToClient(m_hWnd, reinterpret_cast<LPPOINT>(&canvasRect.left));
    ScreenToClient(m_hWnd, reinterpret_cast<LPPOINT>(&canvasRect.right));

    HWND previousCanvas = m_hTreeCanvas;
    HWND newCanvas = CreateWindowEx(
        0,
        L"EDIT",
        L"",
        WS_VISIBLE | WS_CHILD | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | ES_AUTOHSCROLL,
        canvasRect.left,
        canvasRect.top,
        canvasRect.right - canvasRect.left,
        canvasRect.bottom - canvasRect.top,
        m_hWnd,
        reinterpret_cast<HMENU>(ID_TREE_CANVAS),
        m_hInstance,
        nullptr
    );

    if (!newCanvas) {
        SetWindowText(previousCanvas, L"");
        SendMessage(previousCanvas, EM_EMPTYUNDOBUFFER, 0, 0);
        return;
    }

    if (m_hMonoFont) {
        SendMessage(newCanvas, WM_SETFONT, reinterpret_cast<WPARAM>(m_hMonoFont), MAKELPARAM(FALSE, 0));
    }
    SendMessage(newCanvas, EM_SETUNDOLIMIT, 0, 0);
    SendMessage(newCanvas, EM_EMPTYUNDOBUFFER, 0, 0);
    SetWindowSubclass(newCanvas, TreeCanvasSubclassProc, TREE_CANVAS_SUBCLASS_ID, reinterpret_cast<DWORD_PTR>(this));

    RemoveWindowSubclass(previousCanvas, TreeCanvasSubclassProc, TREE_CANVAS_SUBCLASS_ID);
    DestroyWindow(previousCanvas);
    m_hTreeCanvas = newCanvas;
}

void Application::TrimProcessMemoryUsage() {
    _heapmin();
    HeapCompact(GetProcessHeap(), 0);

    HANDLE process = GetCurrentProcess();
    SetProcessWorkingSetSize(process, static_cast<SIZE_T>(-1), static_cast<SIZE_T>(-1));
    EmptyWorkingSet(process);
}

void Application::GenerateTree() {
    // Cancel any running generation
    CancelGeneration();
    
    // Set focus to main window
    SetFocus(m_hWnd);
    
    // Start async generation
    GenerateTreeAsync();
}

void Application::GenerateTreeAsync() {
    // Get generation parameters before compaction to decide if we should shrink aggressively.
    wchar_t depthBuffer[32];
    GetWindowText(m_hDepthEdit, depthBuffer, 32);
    const int depth = _wtoi(depthBuffer);

    m_previousTreeSizeBeforeBuild = m_treeContent.size();
    m_previousTreeCapacityBeforeBuild = m_treeContent.capacity();
    CompactTreeBuffersForNextBuild(depth);
    
    // Mark as generating
    m_isGenerating = true;
    m_animationStep = 0;
    
    // Start progress animation
    SetTimer(m_hWnd, PROGRESS_TIMER_ID, 500, nullptr);
    
    // Disable generate button
    EnableWindow(m_hGenerateBtn, FALSE);
    
    // Show initial progress message
    SetWindowText(m_hTreeCanvas, L"Генерируется дерево");
    SendMessage(m_hTreeCanvas, EM_EMPTYUNDOBUFFER, 0, 0);
    
    std::wstring currentPath = GetCurrentWorkingPath();

    if (!m_treeGenerationService) {
        OnTreeGenerationError(L"Сервис генерации не инициализирован");
        return;
    }

    m_treeGenerationService->Start(
        currentPath,
        depth,
        IsExpandSymlinksEnabled(),
        [this](std::wstring&& result) {
            std::wstring* completedResult = new std::wstring(std::move(result));
            if (!PostMessage(m_hWnd, WM_TREE_COMPLETED, 0, reinterpret_cast<LPARAM>(completedResult))) {
                delete completedResult;
            }
        },
        [this](std::wstring&& error) {
            std::wstring* errorMessage = new std::wstring(std::move(error));
            if (!PostMessage(m_hWnd, WM_TREE_ERROR, 0, reinterpret_cast<LPARAM>(errorMessage))) {
                delete errorMessage;
            }
        }
    );
}

void Application::CancelGeneration() {
    if (m_isGenerating) {
        if (m_treeGenerationService) {
            m_treeGenerationService->Cancel();
        }
        
        // Stop progress timer
        KillTimer(m_hWnd, PROGRESS_TIMER_ID);
        
        // Re-enable generate button
        EnableWindow(m_hGenerateBtn, TRUE);
        
        m_isGenerating = false;
    }
}

void Application::OnTreeGenerationCompleted(const std::wstring& result) {
    // Stop progress animation
    KillTimer(m_hWnd, PROGRESS_TIMER_ID);

    m_treeContent = result;
    if (m_treeContent.capacity() > TREE_LARGE_CHARS_THRESHOLD &&
        m_treeContent.size() < (m_treeContent.capacity() / TREE_SHRINK_FACTOR)) {
        std::wstring compact(m_treeContent);
        m_treeContent.swap(compact);
    }

    const bool droppedByAbsoluteThreshold =
        m_previousTreeSizeBeforeBuild > m_treeContent.size() &&
        (m_previousTreeSizeBeforeBuild - m_treeContent.size()) > TREE_LARGE_CHARS_THRESHOLD;
    const bool droppedByFactor =
        m_previousTreeSizeBeforeBuild > TREE_LARGE_CHARS_THRESHOLD &&
        m_treeContent.size() < (m_previousTreeSizeBeforeBuild / TREE_SHRINK_FACTOR);
    const bool droppedFromLargeTree = droppedByAbsoluteThreshold || droppedByFactor;
    if (droppedFromLargeTree || m_previousTreeCapacityBeforeBuild > TREE_LARGE_CHARS_THRESHOLD) {
        RecreateTreeCanvasControl();
    }
    
    // Update UI first
    SetWindowText(m_hTreeCanvas, m_treeContent.c_str());
    SendMessage(m_hTreeCanvas, EM_EMPTYUNDOBUFFER, 0, 0);
    const bool shouldTrimAfterCompletion = droppedFromLargeTree;
    if (shouldTrimAfterCompletion) {
        TrimProcessMemoryUsage();
    }
    
    // Then show status message after tree is displayed
    ShowStatusMessage(L"Дерево директорий построено");
    
    // Re-enable generate button
    EnableWindow(m_hGenerateBtn, TRUE);
    
    // Set flags
    m_hasGeneratedTree = true;
    m_isGenerating = false;
    
    // Update current depth
    wchar_t buffer[32];
    GetWindowText(m_hDepthEdit, buffer, 32);
    m_currentDepth = _wtoi(buffer);
}

void Application::OnTreeGenerationError(const std::wstring& error) {
    // Stop progress animation
    KillTimer(m_hWnd, PROGRESS_TIMER_ID);
    
    // Update UI
    SetWindowText(m_hTreeCanvas, error.c_str());
    SendMessage(m_hTreeCanvas, EM_EMPTYUNDOBUFFER, 0, 0);
    TrimProcessMemoryUsage();
    ShowStatusMessage(L"Ошибка при построении дерева");
    
    // Re-enable generate button
    EnableWindow(m_hGenerateBtn, TRUE);
    
    m_isGenerating = false;
}

void Application::UpdateProgressAnimation() {
    if (!m_isGenerating) return;
    
    m_animationStep = (m_animationStep + 1) % 4;
    
    std::wstring message = L"Генерируется дерево";
    for (int i = 0; i < m_animationStep; i++) {
        message += L".";
    }
    
    SetWindowText(m_hTreeCanvas, message.c_str());
    
    // Also update status bar with persistent message
    std::wstring statusMessage = L"Построение дерева";
    for (int i = 0; i < m_animationStep; i++) {
        statusMessage += L".";
    }
    ShowPersistentStatusMessage(statusMessage);
}

void Application::CopyToClipboard() {
    if (m_treeContent.empty()) return;

    if (OpenClipboard(m_hWnd)) {
        EmptyClipboard();
        
        size_t size = (m_treeContent.length() + 1) * sizeof(wchar_t);
        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, size);
        if (hMem) {
            void* pMem = GlobalLock(hMem);
            if (pMem) {
                wcscpy_s(static_cast<wchar_t*>(pMem), m_treeContent.length() + 1, m_treeContent.c_str());
                GlobalUnlock(hMem);
                SetClipboardData(CF_UNICODETEXT, hMem);
            }
        }
        CloseClipboard();
        ShowStatusMessage(L"Скопировано в буфер обмена");
    }
}

void Application::SaveToFile() {
    if (m_treeContent.empty()) return;

    OPENFILENAME ofn = {};
    wchar_t szFile[260] = {};
    
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = m_hWnd;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile) / sizeof(wchar_t);
    ofn.lpstrFilter = L"Text Files (*.txt)\0*.txt\0JSON Files (*.json)\0*.json\0XML Files (*.xml)\0*.xml\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrFileTitle = nullptr;
    ofn.nMaxFileTitle = 0;
    ofn.lpstrInitialDir = nullptr;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT;

    if (GetSaveFileName(&ofn)) {
        std::wstring fileName(szFile);
        
        // Determine format based on filter index
        TreeFormat format = TreeFormat::TEXT;
        std::wstring expectedExt = L".txt";
        
        switch (ofn.nFilterIndex) {
            case 2: // JSON
                format = TreeFormat::JSON;
                expectedExt = L".json";
                break;
            case 3: // XML
                format = TreeFormat::XML;
                expectedExt = L".xml";
                break;
            default: // Text
                format = TreeFormat::TEXT;
                expectedExt = L".txt";
                break;
        }
        
        // Add appropriate extension if not present
        if (fileName.length() < expectedExt.length() || 
            fileName.substr(fileName.length() - expectedExt.length()) != expectedExt) {
            fileName += expectedExt;
        }
        
        if (format == TreeFormat::TEXT) {
            // Synchronous save for text format (use existing content)
            SaveFileSync(std::move(fileName), m_treeContent);
        } else {
            // Asynchronous save for JSON/XML formats (need to generate content)
            SaveFileAsync(std::move(fileName), format);
        }
    }
}

void Application::SaveFileSync(std::wstring&& fileName, const std::wstring& content) {
    std::wstring errorMessage;
    if (m_fileSaveService && m_fileSaveService->SaveTextFileSync(fileName, content, &errorMessage)) {
        ShowStatusMessage(L"Файл сохранен");
    } else {
        if (errorMessage.empty()) {
            errorMessage = L"Сервис сохранения не инициализирован";
        }
        ShowStatusMessage(errorMessage);
    }
}

void Application::SaveFileAsync(std::wstring&& fileName, TreeFormat format) {
    if (m_isSaving.load()) return; // Already saving
    
    m_isSaving = true;
    ShowPersistentStatusMessage(L"Сохранение файла...");
    
    // Get generation parameters
    wchar_t depthBuffer[32];
    GetWindowText(m_hDepthEdit, depthBuffer, 32);
    int depth = _wtoi(depthBuffer);
    std::wstring currentPath = GetCurrentWorkingPath();

    if (!m_fileSaveService) {
        OnSaveError(L"Сервис сохранения не инициализирован");
        return;
    }

    m_fileSaveService->SaveTreeAsync(
        fileName,
        currentPath,
        depth,
        format,
        IsExpandSymlinksEnabled(),
        [this]() {
            PostMessage(m_hWnd, WM_SAVE_COMPLETED, 0, 0);
        },
        [this](std::wstring&& error) {
            std::wstring* saveError = new std::wstring(std::move(error));
            if (!PostMessage(m_hWnd, WM_SAVE_ERROR, 0, reinterpret_cast<LPARAM>(saveError))) {
                delete saveError;
            }
        }
    );
}

void Application::OnSaveCompleted() {
    m_isSaving = false;
    ShowStatusMessage(L"Файл сохранен");
}

void Application::OnSaveError(const std::wstring& error) {
    m_isSaving = false;
    ShowStatusMessage(error);
}

void Application::UpdateCurrentPath() {
    std::wstring currentPath = GetCurrentWorkingPath();
    m_lastKnownPath = currentPath;
    SetWindowText(m_hPathEdit, currentPath.c_str());
}

void Application::ShowStatusMessage(const std::wstring& message) {
    SetWindowText(m_hStatusLabel, message.c_str());
    SetTimer(m_hWnd, STATUS_TIMER_ID, 3000, nullptr); // Hide after 3 seconds
}

void Application::ShowPersistentStatusMessage(const std::wstring& message) {
    SetWindowText(m_hStatusLabel, message.c_str());
    // Don't set timer - message will persist until explicitly changed
}

void Application::ShowHelpMenu() {
    if (!m_hMainMenu || !m_hHelpBtn || !IsWindow(m_hHelpBtn)) {
        return;
    }

    RECT buttonRect = {};
    GetWindowRect(m_hHelpBtn, &buttonRect);

    SetForegroundWindow(m_hWnd);
    TrackPopupMenu(
        m_hMainMenu,
        TPM_LEFTALIGN | TPM_TOPALIGN | TPM_LEFTBUTTON,
        buttonRect.left,
        buttonRect.bottom + 2,
        0,
        m_hWnd,
        nullptr
    );

    PostMessage(m_hWnd, WM_NULL, 0, 0);
}

void Application::ShowTextContextMenu(HWND targetControl, LPARAM lParam) {
    if (!targetControl || !IsWindow(targetControl)) {
        return;
    }

    HMENU contextMenu = CreatePopupMenu();
    if (!contextMenu) {
        return;
    }

    AppendMenu(contextMenu, MF_OWNERDRAW, ID_MENU_CONTEXT_COPY, L"Копировать");

    MENUINFO popupMenuInfo = {};
    popupMenuInfo.cbSize = sizeof(MENUINFO);
    popupMenuInfo.fMask = MIM_BACKGROUND;
    popupMenuInfo.hbrBack = m_hEditBrush;
    SetMenuInfo(contextMenu, &popupMenuInfo);

    DWORD selectionStart = 0;
    DWORD selectionEnd = 0;
    SendMessage(targetControl, EM_GETSEL, reinterpret_cast<WPARAM>(&selectionStart), reinterpret_cast<LPARAM>(&selectionEnd));
    const bool hasSelection = selectionEnd > selectionStart;
    const bool hasText = GetWindowTextLength(targetControl) > 0;

    EnableMenuItem(contextMenu, ID_MENU_CONTEXT_COPY, MF_BYCOMMAND | (hasText ? MF_ENABLED : MF_GRAYED));

    POINT popupPoint = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
    if (popupPoint.x == -1 && popupPoint.y == -1) {
        if (!GetCaretPos(&popupPoint)) {
            RECT controlRect = {};
            GetWindowRect(targetControl, &controlRect);
            popupPoint.x = controlRect.left + 10;
            popupPoint.y = controlRect.top + 10;
        } else {
            ClientToScreen(targetControl, &popupPoint);
        }
    }

    HWND popupHostWindow = GetAncestor(targetControl, GA_ROOT);
    if (!popupHostWindow || !IsWindow(popupHostWindow)) {
        popupHostWindow = m_hWnd;
    }
    SetForegroundWindow(popupHostWindow);

    // Owner-draw menu is rendered by the main window handlers (WM_MEASUREITEM/WM_DRAWITEM).
    HWND popupOwner = m_hWnd;
    const UINT selectedCommand = TrackPopupMenu(
        contextMenu,
        TPM_LEFTALIGN | TPM_TOPALIGN | TPM_LEFTBUTTON | TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY,
        popupPoint.x,
        popupPoint.y,
        0,
        popupOwner,
        nullptr
    );

    switch (selectedCommand) {
    case ID_MENU_CONTEXT_COPY:
        if (hasText) {
            const int textLength = GetWindowTextLength(targetControl);
            std::wstring controlText(static_cast<size_t>(textLength) + 1, L'\0');
            GetWindowText(targetControl, controlText.data(), textLength + 1);
            controlText.resize(static_cast<size_t>(textLength));

            std::wstring textToCopy = controlText;
            if (hasSelection) {
                size_t start = static_cast<size_t>(selectionStart);
                size_t end = static_cast<size_t>(selectionEnd);
                if (start > controlText.size()) {
                    start = controlText.size();
                }
                if (end > controlText.size()) {
                    end = controlText.size();
                }
                if (end < start) {
                    end = start;
                }
                textToCopy = controlText.substr(start, end - start);
            }

            if (OpenClipboard(popupHostWindow)) {
                EmptyClipboard();

                const size_t bytes = (textToCopy.size() + 1) * sizeof(wchar_t);
                HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
                if (hMem) {
                    void* pMem = GlobalLock(hMem);
                    if (pMem) {
                        wcscpy_s(static_cast<wchar_t*>(pMem), textToCopy.size() + 1, textToCopy.c_str());
                        GlobalUnlock(hMem);
                        if (!SetClipboardData(CF_UNICODETEXT, hMem)) {
                            GlobalFree(hMem);
                        }
                    } else {
                        GlobalFree(hMem);
                    }
                }

                CloseClipboard();
            }
        }
        break;
    default:
        break;
    }

    DestroyMenu(contextMenu);
    PostMessage(popupOwner, WM_NULL, 0, 0);
}

void Application::ShowHotkeysWindow() {
    const std::wstring hotkeysText =
        L"Глобальные горячие клавиши:\r\n"
        L"Alt+T\t— показать/скрыть окно утилиты\r\n\r\n"
        L"Локальные горячие клавиши (в окне приложения):\r\n"
        L"Enter\t— построить дерево\r\n"
        L"Ctrl+C\t— копировать результат\r\n"
        L"Ctrl+S\t— сохранить результат\r\n"
        L"0-9\t— ввод глубины\r\n"
        L"-\t— смена знака глубины\r\n"
        L"Backspace\t— редактирование глубины\r\n"
        L"Shift+Backspace\t— очистка поля глубины\r\n"
        L"Стрелки\t— прокрутка дерева / изменение глубины\r\n";

    CreateOrActivateInfoWindow(
        InfoWindowKind::Hotkeys,
        m_hHotkeysWindow,
        GetMenuItemText(ID_MENU_HELP_HOTKEYS, 0),
        hotkeysText
    );
}

void Application::ShowAboutWindow() {
    std::wstring aboutText;
    aboutText.reserve(256);
    aboutText += L"Directory Tree Utility\r\n";
    aboutText += L"Версия: ";
    aboutText += APP_VERSION;
    aboutText += L"\r\n";
    aboutText += L"Автор: laynholt\r\n\r\n";
    aboutText += L"Утилита для построения символьного дерева директорий.\r\n";
    aboutText += L"Поддерживает экспорт в TXT / JSON / XML.";

    CreateOrActivateInfoWindow(
        InfoWindowKind::About,
        m_hAboutWindow,
        GetMenuItemText(ID_MENU_HELP_ABOUT, 0),
        aboutText
    );
}

void Application::CreateOrActivateInfoWindow(InfoWindowKind kind, HWND& targetHandle, const wchar_t* title, const std::wstring& bodyText) {
    const wchar_t* effectiveTitle = title;
    if (!effectiveTitle || effectiveTitle[0] == L'\0') {
        const UINT menuItemId = (kind == InfoWindowKind::Hotkeys) ? ID_MENU_HELP_HOTKEYS : ID_MENU_HELP_ABOUT;
        effectiveTitle = GetMenuItemText(menuItemId, 0);
        if (!effectiveTitle) {
            effectiveTitle = L"";
        }
    }

    if (targetHandle && IsWindow(targetHandle)) {
        SetWindowText(targetHandle, effectiveTitle);
        ::ShowWindow(targetHandle, SW_SHOWNORMAL);
        SetForegroundWindow(targetHandle);
        return;
    }

    const int windowWidth = (kind == InfoWindowKind::Hotkeys) ? 620 : 500;
    const int windowHeight = (kind == InfoWindowKind::Hotkeys) ? 420 : 280;

    RECT parentRect;
    GetWindowRect(m_hWnd, &parentRect);
    int x = parentRect.left + ((parentRect.right - parentRect.left) - windowWidth) / 2;
    int y = parentRect.top + ((parentRect.bottom - parentRect.top) - windowHeight) / 2;
    if (x < 0) x = 50;
    if (y < 0) y = 50;

    auto* state = new InfoWindowState{
        this,
        static_cast<int>(kind),
        nullptr,
        nullptr,
        bodyText,
        (m_hInfoFont ? m_hInfoFont : m_hFont),
        nullptr,
        false
    };

    targetHandle = CreateWindowEx(
        WS_EX_DLGMODALFRAME,
        INFO_WINDOW_CLASS,
        effectiveTitle,
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        x, y,
        windowWidth, windowHeight,
        m_hWnd,
        nullptr,
        m_hInstance,
        state
    );

    if (!targetHandle) {
        delete state;
        ShowStatusMessage(L"Не удалось открыть информационное окно");
        return;
    }

    SetWindowText(targetHandle, effectiveTitle);
    ::ShowWindow(targetHandle, SW_SHOW);
    UpdateWindow(targetHandle);
}

void Application::OnInfoWindowClosed(InfoWindowKind kind) {
    if (kind == InfoWindowKind::Hotkeys) {
        m_hHotkeysWindow = nullptr;
    } else {
        m_hAboutWindow = nullptr;
    }
}

LRESULT CALLBACK Application::InfoWindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<InfoWindowState*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));

    switch (message) {
    case WM_NCCREATE:
        {
            CREATESTRUCT* create = reinterpret_cast<CREATESTRUCT*>(lParam);
            auto* initialState = reinterpret_cast<InfoWindowState*>(create->lpCreateParams);
            SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(initialState));
            return TRUE;
        }

    case WM_CREATE:
        if (state) {
            state->editBrush = CreateSolidBrush(RGB(45, 45, 45));
            state->useRichText = (static_cast<InfoWindowKind>(state->kind) == InfoWindowKind::Hotkeys) && (g_richEditModule != nullptr);

            state->textControl = CreateWindowEx(
                0,
                state->useRichText ? MSFTEDIT_CLASS : L"EDIT",
                state->useRichText ? L"" : state->text.c_str(),
                WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_WANTRETURN | ES_READONLY,
                12, 12, 100, 100,
                hWnd,
                reinterpret_cast<HMENU>(ID_INFO_TEXT),
                GetModuleHandle(nullptr),
                nullptr
            );

            state->closeButton = CreateWindowEx(
                0,
                L"BUTTON",
                L"Закрыть",
                WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON | BS_OWNERDRAW,
                12, 12, 120, 30,
                hWnd,
                reinterpret_cast<HMENU>(ID_INFO_CLOSE),
                GetModuleHandle(nullptr),
                nullptr
            );

            if (state->font) {
                SendMessage(state->textControl, WM_SETFONT, reinterpret_cast<WPARAM>(state->font), MAKELPARAM(FALSE, 0));
                SendMessage(state->closeButton, WM_SETFONT, reinterpret_cast<WPARAM>(state->font), MAKELPARAM(FALSE, 0));
            }

            if (state->owner && state->textControl) {
                SetWindowSubclass(
                    state->textControl,
                    TextEditSubclassProc,
                    TEXT_CONTEXT_SUBCLASS_ID,
                    reinterpret_cast<DWORD_PTR>(state->owner)
                );
            }

            if (state->useRichText && state->textControl) {
                ApplyHotkeysFormatting(state->textControl, state->text);
            }
        }
        return 0;

    case WM_SIZE:
        if (state) {
            const int width = LOWORD(lParam);
            const int height = HIWORD(lParam);
            const int margin = 20;
            const int buttonWidth = 120;
            const int buttonHeight = 30;

            MoveWindow(state->textControl, margin, margin, width - (margin * 2), height - (margin * 3) - buttonHeight, TRUE);
            MoveWindow(state->closeButton, width - margin - buttonWidth, height - margin - buttonHeight, buttonWidth, buttonHeight, TRUE);
        }
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);

            RECT clientRect;
            GetClientRect(hWnd, &clientRect);
            UiRenderer::DrawBackground(hdc, clientRect);

            RECT cardRect = {8, 8, clientRect.right - 8, clientRect.bottom - 8};
            UiRenderer::DrawCard(hdc, cardRect);

            EndPaint(hWnd, &ps);

            if (state && state->textControl) {
                UiRenderer::DrawEditBorder(hWnd, state->textControl);
            }
        }
        return 0;

    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
        if (state && state->editBrush) {
            HDC hdcControl = reinterpret_cast<HDC>(wParam);
            SetTextColor(hdcControl, RGB(255, 255, 255));
            SetBkColor(hdcControl, RGB(45, 45, 45));
            return reinterpret_cast<INT_PTR>(state->editBrush);
        }
        break;

    case WM_DRAWITEM:
        {
            DRAWITEMSTRUCT* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
            if (dis && dis->CtlType == ODT_BUTTON && dis->CtlID == ID_INFO_CLOSE) {
                bool isPressed = (dis->itemState & ODS_SELECTED) != 0;
                float hoverAlpha = (dis->itemState & ODS_HOTLIGHT) ? 1.0f : 0.0f;
                UiRenderer::DrawCustomButton(dis->hDC, dis->hwndItem, L"Закрыть", isPressed, hoverAlpha);
                return TRUE;
            }
        }
        break;

    case WM_COMMAND:
        if (LOWORD(wParam) == ID_INFO_CLOSE || LOWORD(wParam) == IDOK) {
            DestroyWindow(hWnd);
            return 0;
        }
        break;

    case WM_CLOSE:
        DestroyWindow(hWnd);
        return 0;

    case WM_DESTROY:
        return 0;

    case WM_NCDESTROY:
        if (state) {
            if (state->editBrush) {
                DeleteObject(state->editBrush);
                state->editBrush = nullptr;
            }
            if (state->owner) {
                state->owner->OnInfoWindowClosed(static_cast<InfoWindowKind>(state->kind));
            }
            delete state;
            SetWindowLongPtr(hWnd, GWLP_USERDATA, 0);
        }
        return DefWindowProc(hWnd, message, wParam, lParam);
    }

    return DefWindowProc(hWnd, message, wParam, lParam);
}



void Application::OnLButtonDown(LPARAM lParam) {
    POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
    
    if (IsPointInButton(m_hGenerateBtn, pt)) {
        m_pressedButton = m_hGenerateBtn;
        InvalidateButton(m_hGenerateBtn);
        SetCapture(m_hWnd);
    } else if (IsPointInButton(m_hCopyBtn, pt)) {
        m_pressedButton = m_hCopyBtn;
        InvalidateButton(m_hCopyBtn);
        SetCapture(m_hWnd);
    } else if (IsPointInButton(m_hSaveBtn, pt)) {
        m_pressedButton = m_hSaveBtn;
        InvalidateButton(m_hSaveBtn);
        SetCapture(m_hWnd);
    } else if (IsPointInButton(m_hHelpBtn, pt)) {
        m_pressedButton = m_hHelpBtn;
        InvalidateButton(m_hHelpBtn);
        SetCapture(m_hWnd);
    }
}

void Application::OnLButtonUp(LPARAM lParam) {
    if (m_pressedButton) {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        
        // Execute button action if mouse is still over the pressed button
        if (IsPointInButton(m_pressedButton, pt)) {
            if (m_pressedButton == m_hGenerateBtn) {
                GenerateTree();
            } else if (m_pressedButton == m_hCopyBtn) {
                CopyToClipboard();
            } else if (m_pressedButton == m_hSaveBtn) {
                SaveToFile();
            } else if (m_pressedButton == m_hHelpBtn) {
                ShowHelpMenu();
            }
        }
        
        InvalidateButton(m_pressedButton);
        m_pressedButton = nullptr;
        ReleaseCapture();
    }
}

bool Application::IsPointInButton(HWND hBtn, POINT pt) {
    if (!hBtn || !IsWindow(hBtn)) {
        return false;
    }

    RECT btnRect;
    GetWindowRect(hBtn, &btnRect);
    ScreenToClient(m_hWnd, reinterpret_cast<LPPOINT>(&btnRect.left));
    ScreenToClient(m_hWnd, reinterpret_cast<LPPOINT>(&btnRect.right));
    return PtInRect(&btnRect, pt);
}

void Application::InvalidateButton(HWND hBtn) {
    // For owner-drawn buttons, we need to force a redraw
    InvalidateRect(hBtn, nullptr, TRUE);
    UpdateWindow(hBtn);
    
    // Also force the parent window to redraw the button area
    RECT btnRect;
    GetWindowRect(hBtn, &btnRect);
    ScreenToClient(m_hWnd, reinterpret_cast<LPPOINT>(&btnRect.left));
    ScreenToClient(m_hWnd, reinterpret_cast<LPPOINT>(&btnRect.right));
    InvalidateRect(m_hWnd, &btnRect, FALSE);
}

void Application::RedrawButtonDirect(HWND hBtn) {
    // Use ValidateRect to prevent automatic background clearing
    RECT rect;
    GetClientRect(hBtn, &rect);
    ValidateRect(hBtn, &rect);
    
    // Get button DC and draw directly without clearing background
    HDC hdc = GetDC(hBtn);
    if (hdc) {
        // Get button text
        wchar_t text[256];
        GetWindowText(hBtn, text, 256);
        
        // Determine button state
        bool isPressed = (m_pressedButton == hBtn);
        
        // Draw directly over existing content
        UiRenderer::DrawCustomButton(hdc, hBtn, text, isPressed, m_buttonHoverAlpha[hBtn]);
        
        ReleaseDC(hBtn, hdc);
    }
}

LRESULT CALLBACK Application::TreeCanvasSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
    UNREFERENCED_PARAMETER(uIdSubclass);
    Application* pApp = reinterpret_cast<Application*>(dwRefData);
    
    switch (uMsg) {
    case WM_CONTEXTMENU:
        if (pApp) {
            pApp->ShowTextContextMenu(hWnd, lParam);
            return 0;
        }
        break;

    case WM_MOUSEWHEEL:
        {
            int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            if (pApp) {
                pApp->HandleMouseWheelScroll(delta);
            }
            return 0;
        }
    }
    
    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

LRESULT CALLBACK Application::DepthEditSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
    UNREFERENCED_PARAMETER(uIdSubclass);
    Application* pApp = reinterpret_cast<Application*>(dwRefData);
    
    switch (uMsg) {
    case WM_CONTEXTMENU:
        if (pApp) {
            pApp->ShowTextContextMenu(hWnd, lParam);
            return 0;
        }
        break;

    case WM_CHAR:
        {
            wchar_t ch = static_cast<wchar_t>(wParam);
            if (ch == L'-' || ch == VK_OEM_MINUS) {
                // Handle minus key - toggle sign of the number
                wchar_t buffer[32];
                GetWindowText(hWnd, buffer, 32);
                int value = _wtoi(buffer);
                swprintf_s(buffer, 32, L"%d", -value);
                SetWindowText(hWnd, buffer);
                pApp->m_isDefaultDepthValue = false;
                pApp->m_hasGeneratedTree = false;
                // Return focus to main window after processing
                SetFocus(pApp->m_hWnd);
                return 0; // Don't let the default handler process this

            } else if (ch == VK_BACK) {
                // Handle backspace key
                if (GetKeyState(VK_SHIFT) & 0x8000) {
                    // Shift+Backspace: clear entire field completely
                    SetWindowText(hWnd, L"");
                    pApp->m_isDefaultDepthValue = false;
                    pApp->m_hasGeneratedTree = false;
                    // Select all text so user can see what happened
                    SendMessage(hWnd, EM_SETSEL, 0, -1);

                } else {
                    // Regular Backspace: delete character before cursor
                    wchar_t buffer[32];
                    GetWindowText(hWnd, buffer, 32);
                    int len = static_cast<int>(wcslen(buffer));
                    if (len > 0) {
                        buffer[len - 1] = L'\0';
                        if (wcslen(buffer) == 0) {
                            wcscpy_s(buffer, L"1");
                            pApp->m_isDefaultDepthValue = true;
                        } else {
                            pApp->m_isDefaultDepthValue = false;
                        }
                        SetWindowText(hWnd, buffer);
                    }
                    pApp->m_hasGeneratedTree = false;
                }
                // Return focus to main window after processing
                SetFocus(pApp->m_hWnd);
                return 0; // Don't let the default handler process this

            } else if (ch >= L'0' && ch <= L'9') {
                // Handle number input
                wchar_t buffer[32];
                GetWindowText(hWnd, buffer, 32);
                
                // If tree was generated, clear the field completely and start fresh
                if (pApp->m_hasGeneratedTree) {
                    swprintf_s(buffer, 32, L"%c", ch);
                    pApp->m_isDefaultDepthValue = false;
                    pApp->m_hasGeneratedTree = false;
                }
                // If it's the default value "1" and user types a different digit, replace it
                else if (pApp->m_isDefaultDepthValue && wcscmp(buffer, L"1") == 0 && ch != '1') {
                    swprintf_s(buffer, 32, L"%c", ch);
                    pApp->m_isDefaultDepthValue = false;
                }
                // If it's empty or zero, just set the digit
                else if (wcslen(buffer) == 0 || (wcslen(buffer) == 1 && buffer[0] == L'0')) {
                    swprintf_s(buffer, 32, L"%c", ch);
                    pApp->m_isDefaultDepthValue = false;
                }
                // Otherwise, append the digit unless it would result in leading zero
                else {
                    if (!(buffer[0] == L'0' && wcslen(buffer) == 1)) {
                        size_t len = wcslen(buffer);
                        if (len < 31) { // Leave space for null terminator
                            buffer[len] = ch;
                            buffer[len + 1] = L'\0';
                        }
                    }
                    pApp->m_isDefaultDepthValue = false;
                }
                
                SetWindowText(hWnd, buffer);
                // Position cursor at the end
                int len = static_cast<int>(wcslen(buffer));
                SendMessage(hWnd, EM_SETSEL, len, len);
                return 0; // Don't let the default handler process this
            }
            // Allow only numbers, minus, and backspace - block other characters
            else if (ch < 32) {
                // Allow control characters (like Ctrl+A, Ctrl+C, etc.)
                break;
            } else {
                // Block all other printable characters
                return 0;
            }
            break;
        }
    case WM_KEYDOWN:
        {
            if (wParam == VK_UP) {
                // Handle up arrow - increment value
                wchar_t buffer[32];
                GetWindowText(hWnd, buffer, 32);
                int value = _wtoi(buffer);
                
                // Increment the value (max reasonable depth would be around 50)
                if (value < 50) {
                    value++;
                    swprintf_s(buffer, 32, L"%d", value);
                    SetWindowText(hWnd, buffer);
                    pApp->m_isDefaultDepthValue = false;
                    pApp->m_hasGeneratedTree = false;
                    
                    // Position cursor at the end
                    int len = static_cast<int>(wcslen(buffer));
                    SendMessage(hWnd, EM_SETSEL, len, len);
                }
                return 0; // Don't let the default handler process this
            } else if (wParam == VK_DOWN) {
                // Handle down arrow - decrement value
                wchar_t buffer[32];
                GetWindowText(hWnd, buffer, 32);
                int value = _wtoi(buffer);
                
                // Decrement the value (minimum of 1 for positive, unlimited for negative)
                if (value > 1 || value < 0) {
                    value--;
                    // Don't let it go to 0, jump to -1 instead
                    if (value == 0) {
                        value = -1;
                    }
                    swprintf_s(buffer, 32, L"%d", value);
                    SetWindowText(hWnd, buffer);
                    pApp->m_isDefaultDepthValue = false;
                    pApp->m_hasGeneratedTree = false;
                    
                    // Position cursor at the end
                    int len = static_cast<int>(wcslen(buffer));
                    SendMessage(hWnd, EM_SETSEL, len, len);
                }
                return 0; // Don't let the default handler process this
            }
            // Let Left/Right arrows work normally for cursor movement
            break;
        }
    }
    
    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

LRESULT CALLBACK Application::TextEditSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
    UNREFERENCED_PARAMETER(uIdSubclass);
    UNREFERENCED_PARAMETER(wParam);
    Application* pApp = reinterpret_cast<Application*>(dwRefData);

    if (uMsg == WM_CONTEXTMENU && pApp) {
        pApp->ShowTextContextMenu(hWnd, lParam);
        return 0;
    }

    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

void Application::ScrollCanvas(int direction) {
    switch (direction) {
    case 0: // up
        SendMessage(m_hTreeCanvas, WM_VSCROLL, SB_LINEUP, 0);
        break;
    case 1: // down
        SendMessage(m_hTreeCanvas, WM_VSCROLL, SB_LINEDOWN, 0);
        break;
    case 2: // left
        SendMessage(m_hTreeCanvas, WM_HSCROLL, SB_LINELEFT, 0);
        break;
    case 3: // right
        SendMessage(m_hTreeCanvas, WM_HSCROLL, SB_LINERIGHT, 0);
        break;
    }
}

std::wstring Application::GetCurrentWorkingPath() {
    std::wstring currentPath = FileExplorerIntegration::GetActiveExplorerPath();
    if (currentPath.empty()) {
        wchar_t buffer[MAX_PATH];
        GetCurrentDirectory(MAX_PATH, buffer);
        currentPath = buffer;
    }
    return currentPath;
}

void Application::HandleMouseWheelScroll(int delta) {
    int scrollLines = 3; // Number of lines to scroll
    
    if (delta > 0) {
        // Scroll up
        for (int i = 0; i < scrollLines; i++) {
            ScrollCanvas(0);
        }
    } else {
        // Scroll down
        for (int i = 0; i < scrollLines; i++) {
            ScrollCanvas(1);
        }
    }
}

bool Application::IsExpandSymlinksEnabled() const {
    return m_expandSymlinks;
}

