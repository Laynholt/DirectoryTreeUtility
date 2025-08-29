#include "Application.h"
#include "DirectoryTreeBuilder.h"
#include "SystemTray.h"
#include "GlobalHotkeys.h"
#include "FileExplorerIntegration.h"
#include "resource.h"
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <gdiplus.h>
#include <uxtheme.h>

#pragma comment(lib, "comctl32.lib")

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "uxtheme.lib")

using namespace Gdiplus;

const wchar_t* WINDOW_CLASS = L"DirectoryTreeUtilityClass";
const wchar_t* WINDOW_TITLE = L"Directory Tree Utility";

enum ControlIDs {
    ID_DEPTH_EDIT = 1001,
    ID_PATH_EDIT = 1002,
    ID_GENERATE_BTN = 1003,
    ID_COPY_BTN = 1004,
    ID_SAVE_BTN = 1005,
    ID_TREE_CANVAS = 1006
};

Application::Application()
    : m_hInstance(nullptr)
    , m_hWnd(nullptr)
    , m_hDepthEdit(nullptr)
    , m_hPathEdit(nullptr)
    , m_hGenerateBtn(nullptr)
    , m_hCopyBtn(nullptr)
    , m_hSaveBtn(nullptr)
    , m_hTreeCanvas(nullptr)
    , m_currentDepth(1)
    , m_isMinimized(false)
    , m_isDefaultDepthValue(true)
    , m_hasGeneratedTree(false)
    , m_gdiplusToken(0)
    , m_hoveredButton(nullptr)
    , m_pressedButton(nullptr)
    , m_hBgBrush(nullptr)
    , m_hEditBrush(nullptr)
    , m_hStaticBrush(nullptr) {
    
    // Initialize dark theme brushes
    m_hBgBrush = CreateSolidBrush(RGB(26, 26, 26));      // --bg-color: #1a1a1a
    m_hEditBrush = CreateSolidBrush(RGB(45, 45, 45));    // --card-bg: #2d2d2d
    m_hStaticBrush = CreateSolidBrush(RGB(26, 26, 26));  // Same as background
}

Application::~Application() {
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
    wcex.hbrBackground = CreateSolidBrush(RGB(26, 26, 26)); // --bg-color: #1a1a1a
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

    CreateControls();
    
    m_treeBuilder = std::make_unique<DirectoryTreeBuilder>();
    m_systemTray = std::make_unique<SystemTray>(this);
    m_globalHotkeys = std::make_unique<GlobalHotkeys>(this);
    
    if (!m_systemTray->Initialize()) {
        MessageBox(nullptr, L"Ошибка инициализации системного трея", L"Отладка", MB_OK);
        return false;
    }
    
    if (!m_globalHotkeys->Initialize()) {
        MessageBox(nullptr, L"Ошибка инициализации горячих клавиш", L"Отладка", MB_OK);
        return false;
    }

    UpdateCurrentPath();

    // Start timer for dynamic path updating (check every 2 seconds)
    SetTimer(m_hWnd, PATH_UPDATE_TIMER_ID, 2000, nullptr);

    return true;
}

void Application::CreateControls() {
    // Create modern font matching the HTML example
    HFONT hFont = CreateFont(
        -15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI"
    );
    
    // Create labels positioned inside input card without extra space for title
    HWND hDepthLabel = CreateWindowEx(0, L"STATIC", L"Глубина дерева:",
                  WS_VISIBLE | WS_CHILD,
                  24, 18, 140, 20, m_hWnd, nullptr, m_hInstance, nullptr);
    
    HWND hPathLabel = CreateWindowEx(0, L"STATIC", L"Текущий путь:",
                  WS_VISIBLE | WS_CHILD,
                  24, 48, 140, 20, m_hWnd, nullptr, m_hInstance, nullptr);

    // Enhanced depth input positioned inside card
    m_hDepthEdit = CreateWindowEx(0, L"EDIT", L"1",
                                 WS_VISIBLE | WS_CHILD | ES_NUMBER,
                                 174, 16, 80, 24, m_hWnd, (HMENU)ID_DEPTH_EDIT, m_hInstance, nullptr);

    // Enhanced path display inside card
    m_hPathEdit = CreateWindowEx(0, L"EDIT", L"",
                                WS_VISIBLE | WS_CHILD | ES_READONLY,
                                174, 46, 480, 24, m_hWnd, (HMENU)ID_PATH_EDIT, m_hInstance, nullptr);

    // Buttons positioned inside button card - larger size and vertically centered
    m_hGenerateBtn = CreateWindowEx(0, L"BUTTON", L"Построить дерево",
                                   WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
                                   100, 104, 170, 32, m_hWnd, (HMENU)ID_GENERATE_BTN, m_hInstance, nullptr);

    m_hCopyBtn = CreateWindowEx(0, L"BUTTON", L"Копировать",
                               WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
                               280, 104, 150, 32, m_hWnd, (HMENU)ID_COPY_BTN, m_hInstance, nullptr);

    m_hSaveBtn = CreateWindowEx(0, L"BUTTON", L"Сохранить",
                               WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
                               440, 104, 150, 32, m_hWnd, (HMENU)ID_SAVE_BTN, m_hInstance, nullptr);

    // Tree canvas positioned inside tree card - scrollbars appear automatically when needed
    m_hTreeCanvas = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                  WS_VISIBLE | WS_CHILD |
                                  ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | ES_AUTOHSCROLL,
                                  24, 176, 740, 300, m_hWnd, (HMENU)ID_TREE_CANVAS, m_hInstance, nullptr);
    
    // Subclass the tree canvas to handle mouse wheel
    SetWindowSubclass(m_hTreeCanvas, TreeCanvasSubclassProc, 1, (DWORD_PTR)this);

    // Status label positioned inside status card
    m_hStatusLabel = CreateWindowEx(0, L"STATIC", L"Готово",
                                   WS_VISIBLE | WS_CHILD,
                                   24, 488, 740, 20, m_hWnd, nullptr, m_hInstance, nullptr);

    // Apply the modern font to all controls
    SendMessage(m_hDepthEdit, WM_SETFONT, (WPARAM)hFont, MAKELPARAM(FALSE, 0));
    SendMessage(m_hPathEdit, WM_SETFONT, (WPARAM)hFont, MAKELPARAM(FALSE, 0));
    SendMessage(m_hStatusLabel, WM_SETFONT, (WPARAM)hFont, MAKELPARAM(FALSE, 0));
    
    // Apply font to static labels too
    EnumChildWindows(m_hWnd, [](HWND hwndChild, LPARAM lParam) -> BOOL {
        wchar_t className[256];
        GetClassName(hwndChild, className, 256);
        if (wcscmp(className, L"Static") == 0) {
            SendMessage(hwndChild, WM_SETFONT, lParam, MAKELPARAM(FALSE, 0));
        }
        return TRUE;
    }, (LPARAM)hFont);

    // Enhanced monospace font for tree display
    HFONT hMonoFont = CreateFont(
        -15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas"
    );
    SendMessage(m_hTreeCanvas, WM_SETFONT, (WPARAM)hMonoFont, MAKELPARAM(FALSE, 0));
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
                    ShowStatusMessage(L"Дерево директорий построено");
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
                else if (msg.wParam >= VK_LEFT && msg.wParam <= VK_DOWN) {
                    // Check if depth edit has focus - use arrows for value change
                    if (msg.hwnd == m_hDepthEdit || GetFocus() == m_hDepthEdit) {
                        switch (msg.wParam) {
                        case VK_UP:
                            HandleDepthIncrement();
                            break;
                        case VK_DOWN:
                            HandleDepthDecrement();
                            break;
                        // Left/Right arrows work normally for cursor movement in edit control
                        }
                        // Don't continue here - let the edit control handle left/right arrows normally
                        if (msg.wParam == VK_UP || msg.wParam == VK_DOWN) {
                            continue;
                        }
                    } else {
                        // Arrow keys for scrolling canvas when not in depth edit
                        switch (msg.wParam) {
                        case VK_UP:
                            SendMessage(m_hTreeCanvas, WM_VSCROLL, SB_LINEUP, 0);
                            break;
                        case VK_DOWN:
                            SendMessage(m_hTreeCanvas, WM_VSCROLL, SB_LINEDOWN, 0);
                            break;
                        case VK_LEFT:
                            SendMessage(m_hTreeCanvas, WM_HSCROLL, SB_LINELEFT, 0);
                            break;
                        case VK_RIGHT:
                            SendMessage(m_hTreeCanvas, WM_HSCROLL, SB_LINERIGHT, 0);
                            break;
                        }
                        continue;
                    }
                }
                else if (msg.wParam >= '0' && msg.wParam <= '9' && msg.hwnd != m_hDepthEdit) {
                    // Number input - redirect to depth edit
                    SetFocus(m_hDepthEdit);
                    HandleNumberInput((wchar_t)msg.wParam);
                    continue;
                }
                else if (msg.wParam == VK_BACK) {
                    if (msg.hwnd == m_hDepthEdit || GetFocus() == m_hDepthEdit) {
                        // Handle Shift+Backspace in depth edit
                        HandleBackspace();
                        continue;
                    } else {
                        // Backspace - redirect to depth edit
                        SetFocus(m_hDepthEdit);
                        HandleBackspace();
                        continue;
                    }
                }
                else if (msg.wParam == VK_OEM_MINUS && msg.hwnd != m_hDepthEdit) {
                    // Minus key - redirect to depth edit
                    SetFocus(m_hDepthEdit);
                    HandleMinusKey();
                    continue;
                }
            }
        }
        
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return (int)msg.wParam;
}

void Application::Shutdown() {
    // Remove subclassing
    if (m_hTreeCanvas) {
        RemoveWindowSubclass(m_hTreeCanvas, TreeCanvasSubclassProc, 1);
    }
    
    if (m_globalHotkeys) {
        m_globalHotkeys->Cleanup();
        m_globalHotkeys.reset();
    }
    
    if (m_systemTray) {
        m_systemTray->Cleanup();
        m_systemTray.reset();
    }
    
    if (m_treeBuilder) {
        m_treeBuilder.reset();
    }
    
    // Clean up brushes
    if (m_hBgBrush) DeleteObject(m_hBgBrush);
    if (m_hEditBrush) DeleteObject(m_hEditBrush);
    if (m_hStaticBrush) DeleteObject(m_hStaticBrush);
    
    // Shutdown GDI+
    if (m_gdiplusToken) {
        GdiplusShutdown(m_gdiplusToken);
    }
    
    // Uninitialize COM
    CoUninitialize();
}

void Application::ShowWindow(bool show) {
    if (show) {
        ::ShowWindow(m_hWnd, SW_SHOW);
        ::SetForegroundWindow(m_hWnd);
        m_isMinimized = false;
    } else {
        ::ShowWindow(m_hWnd, SW_HIDE);
        m_isMinimized = true;
    }
}

void Application::ToggleVisibility() {
    if (m_isMinimized || !IsWindowVisible(m_hWnd)) {
        // Window is hidden or minimized - show it
        ShowWindow(true);
    } else if (GetForegroundWindow() == m_hWnd) {
        // Window is visible and active - hide it
        ShowWindow(false);
        m_systemTray->AddToTray();
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
        CREATESTRUCT* pCreate = (CREATESTRUCT*)lParam;
        pApp = (Application*)pCreate->lpCreateParams;
        SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)pApp);
        return DefWindowProc(hWnd, message, wParam, lParam);
    } else {
        pApp = (Application*)GetWindowLongPtr(hWnd, GWLP_USERDATA);
    }

    if (pApp) {
        return pApp->HandleMessage(message, wParam, lParam);
    }

    return DefWindowProc(hWnd, message, wParam, lParam);
}

LRESULT Application::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_COMMAND:
        if (HIWORD(wParam) == EN_SETFOCUS && (HWND)lParam == m_hDepthEdit) {
            // When depth edit gets focus, select all text for easy replacement
            SendMessage(m_hDepthEdit, EM_SETSEL, 0, -1);
        }
        OnCommand(LOWORD(wParam));
        break;

    case WM_DRAWITEM:
        {
            DRAWITEMSTRUCT* dis = (DRAWITEMSTRUCT*)lParam;
            if (dis->CtlType == ODT_BUTTON) {
                std::wstring buttonText;
                wchar_t text[256];
                GetWindowText(dis->hwndItem, text, 256);
                buttonText = text;
                
                bool isHovered = (m_hoveredButton == dis->hwndItem);
                bool isPressed = (m_pressedButton == dis->hwndItem) || (dis->itemState & ODS_SELECTED);
                
                DrawCustomButton(dis->hDC, dis->hwndItem, buttonText, isHovered, isPressed);
                return TRUE;
            }
        }
        break;

    case WM_MOUSEMOVE:
        OnMouseMove(lParam);
        break;

    case WM_LBUTTONDOWN:
        OnLButtonDown(lParam);
        break;

    case WM_LBUTTONUP:
        OnLButtonUp(lParam);
        break;

    case WM_CTLCOLORSTATIC:
        {
            // Dark theme for static text (labels) - same background as cards
            HDC hdcStatic = (HDC)wParam;
            SetTextColor(hdcStatic, RGB(255, 255, 255));      // White text
            SetBkColor(hdcStatic, RGB(45, 45, 45));           // Card background color
            return (INT_PTR)m_hEditBrush;  // Use same brush as cards
        }
        
    case WM_CTLCOLOREDIT:
        {
            // Dark theme for edit controls - --card-bg: #2d2d2d with --text-color: #ffffff
            HDC hdcEdit = (HDC)wParam;
            SetTextColor(hdcEdit, RGB(255, 255, 255));        // White text
            SetBkColor(hdcEdit, RGB(45, 45, 45));             // Dark card background
            return (INT_PTR)m_hEditBrush;
        }

    case WM_SIZE:
        OnResize(LOWORD(lParam), HIWORD(lParam));
        break;

    case WM_PAINT:
        OnPaint();
        // Draw dark borders around edit controls
        DrawEditBorder(m_hDepthEdit);
        DrawEditBorder(m_hPathEdit);
        DrawEditBorder(m_hTreeCanvas);
        break;

    case WM_KEYDOWN:
        OnKeyDown(wParam, lParam);
        break;
        
    case WM_CHAR:
        // Handle character input for depth edit when any control has focus
        if (wParam >= L'0' && wParam <= L'9') {
            OnKeyDown(wParam, lParam);
        }
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
                int scrollLines = 3; // Number of lines to scroll
                
                if (delta > 0) {
                    // Scroll up
                    for (int i = 0; i < scrollLines; i++) {
                        SendMessage(m_hTreeCanvas, WM_VSCROLL, SB_LINEUP, 0);
                    }
                } else {
                    // Scroll down
                    for (int i = 0; i < scrollLines; i++) {
                        SendMessage(m_hTreeCanvas, WM_VSCROLL, SB_LINEDOWN, 0);
                    }
                }
                return 0;
            }
        }
        break;

    case WM_GETMINMAXINFO:
        {
            MINMAXINFO* mmi = (MINMAXINFO*)lParam;
            mmi->ptMinTrackSize.x = MIN_WIDTH;
            mmi->ptMinTrackSize.y = MIN_HEIGHT;
        }
        break;

    case WM_CLOSE:
        ShowWindow(false);
        m_systemTray->AddToTray();
        return 0;

    case WM_TIMER:
        if (wParam == STATUS_TIMER_ID) {
            SetWindowText(m_hStatusLabel, L"Готово");
            KillTimer(m_hWnd, STATUS_TIMER_ID);
        }
        else if (wParam == PATH_UPDATE_TIMER_ID) {
            std::wstring currentPath = FileExplorerIntegration::GetActiveExplorerPath();
            if (currentPath.empty()) {
                wchar_t buffer[MAX_PATH];
                GetCurrentDirectory(MAX_PATH, buffer);
                currentPath = buffer;
            }
            
            if (currentPath != m_lastKnownPath) {
                m_lastKnownPath = currentPath;
                SetWindowText(m_hPathEdit, currentPath.c_str());
            }
        }
        break;

    case WM_NCCALCSIZE:
        // Let Windows handle the default calculation
        return DefWindowProc(m_hWnd, message, wParam, lParam);
        
    case WM_NCACTIVATE:
        // Ensure title bar is properly activated/deactivated
        return DefWindowProc(m_hWnd, message, wParam, lParam);

    case WM_DESTROY:
        KillTimer(m_hWnd, STATUS_TIMER_ID);
        KillTimer(m_hWnd, PATH_UPDATE_TIMER_ID);
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProc(m_hWnd, message, wParam, lParam);
    }
    return 0;
}

void Application::OnResize(int width, int height) {
    if (width > 0 && height > 0) {
        // Resize path edit inside input card
        MoveWindow(m_hPathEdit, 174, 46, width - 190, 24, TRUE);
        
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
    DrawBackground(hdc, clientRect);
    
    // Draw input controls card without title
    RECT inputCard = {8, 8, clientRect.right - 8, 84};
    DrawCard(hdc, inputCard);
    
    // Draw buttons card without title
    RECT buttonCard = {8, 92, clientRect.right - 8, 140};
    DrawCard(hdc, buttonCard);
    
    // Draw tree display card - main content area
    RECT treeCard = {8, 148, clientRect.right - 8, clientRect.bottom - 60};
    DrawCard(hdc, treeCard, L"Дерево каталогов");
    
    // Draw status card
    RECT statusCard = {8, clientRect.bottom - 52, clientRect.right - 8, clientRect.bottom - 8};
    DrawCard(hdc, statusCard);
    
    EndPaint(m_hWnd, &ps);
}

void Application::OnCommand(int commandId) {
    switch (commandId) {
    case ID_GENERATE_BTN:
        GenerateTree();
        break;
    case ID_COPY_BTN:
        CopyToClipboard();
        break;
    case ID_SAVE_BTN:
        SaveToFile();
        break;
    }
}

void Application::OnKeyDown(WPARAM key, LPARAM) {
    HWND hFocused = GetFocus();

    switch (key) {
    case VK_ESCAPE:
        SetFocus(m_hWnd);
        break;
    case VK_RETURN:
        GenerateTree();
        break;
    case 'C':
        if (GetKeyState(VK_CONTROL) & 0x8000) {
            CopyToClipboard();
        }
        break;
    case 'S':
        if (GetKeyState(VK_CONTROL) & 0x8000) {
            SaveToFile();
        }
        break;
    case VK_BACK:
        if (hFocused != m_hDepthEdit) {
            SetFocus(m_hDepthEdit);
        }
        if (GetKeyState(VK_SHIFT) & 0x8000) {
            SetWindowText(m_hDepthEdit, L"1");
            m_isDefaultDepthValue = true;
        } else {
            wchar_t buffer[32];
            GetWindowText(m_hDepthEdit, buffer, 32);
            int len = static_cast<int>(wcslen(buffer));
            if (len > 0) {
                buffer[len - 1] = L'\0';
                if (wcslen(buffer) == 0) {
                    wcscpy_s(buffer, L"1");
                    m_isDefaultDepthValue = true;
                } else {
                    m_isDefaultDepthValue = false;
                }
                SetWindowText(m_hDepthEdit, buffer);
            }
        }
        break;
    case VK_OEM_MINUS:
        {
            wchar_t buffer[32];
            GetWindowText(m_hDepthEdit, buffer, 32);
            int value = _wtoi(buffer);
            swprintf_s(buffer, 32, L"%d", -value);
            SetWindowText(m_hDepthEdit, buffer);
            m_isDefaultDepthValue = false;
        }
        break;
    case VK_UP:
        SendMessage(m_hTreeCanvas, WM_VSCROLL, SB_LINEUP, 0);
        break;
    case VK_DOWN:
        SendMessage(m_hTreeCanvas, WM_VSCROLL, SB_LINEDOWN, 0);
        break;
    case VK_LEFT:
        SendMessage(m_hTreeCanvas, WM_HSCROLL, SB_LINELEFT, 0);
        break;
    case VK_RIGHT:
        SendMessage(m_hTreeCanvas, WM_HSCROLL, SB_LINERIGHT, 0);
        break;
    default:
        if (key >= '0' && key <= '9' && hFocused != m_hDepthEdit) {
            // Only handle number input if depth edit is not focused
            SetFocus(m_hDepthEdit);
            wchar_t buffer[32];
            GetWindowText(m_hDepthEdit, buffer, 32);
            
            // If it's the default value "1" and user types a different digit, replace it
            if (m_isDefaultDepthValue && wcscmp(buffer, L"1") == 0 && key != '1') {
                swprintf_s(buffer, 32, L"%c", (wchar_t)key);
                m_isDefaultDepthValue = false;
            }
            // If it's empty or zero, just set the digit
            else if (wcslen(buffer) == 0 || (wcslen(buffer) == 1 && buffer[0] == L'0')) {
                swprintf_s(buffer, 32, L"%c", (wchar_t)key);
                m_isDefaultDepthValue = false;
            }
            // Otherwise append the digit
            else {
                wchar_t newChar[2] = { (wchar_t)key, L'\0' };
                wcscat_s(buffer, 32, newChar);
                m_isDefaultDepthValue = false;
            }
            SetWindowText(m_hDepthEdit, buffer);
        }
        break;
    }
}

void Application::GenerateTree() {
    wchar_t depthBuffer[32];
    GetWindowText(m_hDepthEdit, depthBuffer, 32);
    m_currentDepth = _wtoi(depthBuffer);

    std::wstring currentPath = FileExplorerIntegration::GetActiveExplorerPath();
    if (currentPath.empty()) {
        wchar_t buffer[MAX_PATH];
        GetCurrentDirectory(MAX_PATH, buffer);
        currentPath = buffer;
    }

    m_treeContent = m_treeBuilder->BuildTree(currentPath, m_currentDepth);
    SetWindowText(m_hTreeCanvas, m_treeContent.c_str());
    ShowStatusMessage(L"Дерево директорий построено");
    
    // Set flag that tree was generated
    m_hasGeneratedTree = true;
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
                wcscpy_s((wchar_t*)pMem, m_treeContent.length() + 1, m_treeContent.c_str());
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
    ofn.lpstrFilter = L"Text Files (*.txt)\0*.txt\0All Files (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrFileTitle = nullptr;
    ofn.nMaxFileTitle = 0;
    ofn.lpstrInitialDir = nullptr;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT;

    if (GetSaveFileName(&ofn)) {
        HANDLE hFile = CreateFile(szFile, GENERIC_WRITE, 0, nullptr,
                                 CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hFile != INVALID_HANDLE_VALUE) {
            DWORD bytesWritten;
            std::string utf8Content;
            int utf8Length = WideCharToMultiByte(CP_UTF8, 0, m_treeContent.c_str(), -1, nullptr, 0, nullptr, nullptr);
            if (utf8Length > 0) {
                utf8Content.resize(utf8Length - 1);
                WideCharToMultiByte(CP_UTF8, 0, m_treeContent.c_str(), -1, &utf8Content[0], utf8Length, nullptr, nullptr);
            }
            WriteFile(hFile, utf8Content.c_str(), (DWORD)utf8Content.length(), &bytesWritten, nullptr);
            CloseHandle(hFile);
            ShowStatusMessage(L"Файл сохранен");
        }
    }
}

void Application::UpdateCurrentPath() {
    std::wstring currentPath = FileExplorerIntegration::GetActiveExplorerPath();
    if (currentPath.empty()) {
        wchar_t buffer[MAX_PATH];
        GetCurrentDirectory(MAX_PATH, buffer);
        currentPath = buffer;
    }
    m_lastKnownPath = currentPath;
    SetWindowText(m_hPathEdit, currentPath.c_str());
}

void Application::ShowStatusMessage(const std::wstring& message) {
    SetWindowText(m_hStatusLabel, message.c_str());
    SetTimer(m_hWnd, STATUS_TIMER_ID, 3000, nullptr); // Hide after 3 seconds
}

void Application::HandleNumberInput(wchar_t digit) {
    wchar_t buffer[32];
    GetWindowText(m_hDepthEdit, buffer, 32);
    
    // If tree was generated, clear the field completely and start fresh
    if (m_hasGeneratedTree) {
        swprintf_s(buffer, 32, L"%c", digit);
        m_isDefaultDepthValue = false;
        m_hasGeneratedTree = false;  // Reset the flag
    }
    // If it's the default value "1" and user types a different digit, replace it
    else if (m_isDefaultDepthValue && wcscmp(buffer, L"1") == 0 && digit != '1') {
        swprintf_s(buffer, 32, L"%c", digit);
        m_isDefaultDepthValue = false;
    }
    // If it's empty or zero, just set the digit
    else if (wcslen(buffer) == 0 || (wcslen(buffer) == 1 && buffer[0] == L'0')) {
        swprintf_s(buffer, 32, L"%c", digit);
        m_isDefaultDepthValue = false;
    }
    // Otherwise append the digit
    else {
        wchar_t newChar[2] = { digit, L'\0' };
        wcscat_s(buffer, 32, newChar);
        m_isDefaultDepthValue = false;
    }
    SetWindowText(m_hDepthEdit, buffer);
}

void Application::HandleBackspace() {
    if (GetKeyState(VK_SHIFT) & 0x8000) {
        // Shift+Backspace: clear entire field completely
        SetWindowText(m_hDepthEdit, L"");
        m_isDefaultDepthValue = false;
        m_hasGeneratedTree = false;  // Reset flag
        // Select all text so user can see what happened
        SendMessage(m_hDepthEdit, EM_SETSEL, 0, -1);
    } else {
        // Regular Backspace: delete character before cursor
        wchar_t buffer[32];
        GetWindowText(m_hDepthEdit, buffer, 32);
        int len = static_cast<int>(wcslen(buffer));
        if (len > 0) {
            buffer[len - 1] = L'\0';
            if (wcslen(buffer) == 0) {
                wcscpy_s(buffer, L"1");
                m_isDefaultDepthValue = true;
            } else {
                m_isDefaultDepthValue = false;
            }
            SetWindowText(m_hDepthEdit, buffer);
        }
        m_hasGeneratedTree = false;  // Reset flag on any edit
    }
}

void Application::HandleMinusKey() {
    wchar_t buffer[32];
    GetWindowText(m_hDepthEdit, buffer, 32);
    int value = _wtoi(buffer);
    swprintf_s(buffer, 32, L"%d", -value);
    SetWindowText(m_hDepthEdit, buffer);
    m_isDefaultDepthValue = false;
}

void Application::HandleDepthIncrement() {
    wchar_t buffer[32];
    GetWindowText(m_hDepthEdit, buffer, 32);
    int value = _wtoi(buffer);
    
    // Increment the value (max reasonable depth would be around 50)
    if (value < 50) {
        value++;
        swprintf_s(buffer, 32, L"%d", value);
        SetWindowText(m_hDepthEdit, buffer);
        m_isDefaultDepthValue = false;
        m_hasGeneratedTree = false;  // Reset flag
        
        // Position cursor at the end
        int len = static_cast<int>(wcslen(buffer));
        SendMessage(m_hDepthEdit, EM_SETSEL, len, len);
    }
}

void Application::HandleDepthDecrement() {
    wchar_t buffer[32];
    GetWindowText(m_hDepthEdit, buffer, 32);
    int value = _wtoi(buffer);
    
    // Decrement the value (minimum of 1 for positive, unlimited for negative)
    if (value > 1 || value < 0) {
        value--;
        // Don't let it go to 0, jump to -1 instead
        if (value == 0) {
            value = -1;
        }
        swprintf_s(buffer, 32, L"%d", value);
        SetWindowText(m_hDepthEdit, buffer);
        m_isDefaultDepthValue = false;
        m_hasGeneratedTree = false;  // Reset flag
        
        // Position cursor at the end
        int len = static_cast<int>(wcslen(buffer));
        SendMessage(m_hDepthEdit, EM_SETSEL, len, len);
    }
}

void Application::OnMouseMove(LPARAM lParam) {
    POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
    HWND previousHovered = m_hoveredButton;
    m_hoveredButton = nullptr;
    
    // Check if mouse is over any button
    if (IsPointInButton(m_hGenerateBtn, pt)) {
        m_hoveredButton = m_hGenerateBtn;
    } else if (IsPointInButton(m_hCopyBtn, pt)) {
        m_hoveredButton = m_hCopyBtn;
    } else if (IsPointInButton(m_hSaveBtn, pt)) {
        m_hoveredButton = m_hSaveBtn;
    }
    
    // Redraw buttons if hover state changed
    if (previousHovered != m_hoveredButton) {
        if (previousHovered) InvalidateButton(previousHovered);
        if (m_hoveredButton) InvalidateButton(m_hoveredButton);
        
        // Set cursor
        SetCursor(LoadCursor(nullptr, m_hoveredButton ? IDC_HAND : IDC_ARROW));
    }
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
    }
}

void Application::OnLButtonUp(LPARAM lParam) {
    if (m_pressedButton) {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        
        // Execute button action if mouse is still over the pressed button
        if (IsPointInButton(m_pressedButton, pt)) {
            if (m_pressedButton == m_hGenerateBtn) {
                GenerateTree();
                ShowStatusMessage(L"Дерево директорий построено");
            } else if (m_pressedButton == m_hCopyBtn) {
                CopyToClipboard();
            } else if (m_pressedButton == m_hSaveBtn) {
                SaveToFile();
            }
        }
        
        InvalidateButton(m_pressedButton);
        m_pressedButton = nullptr;
        ReleaseCapture();
    }
}

void Application::DrawCustomButton(HDC hdc, HWND hBtn, const std::wstring& text, bool isHovered, bool isPressed) {
    RECT rect;
    GetClientRect(hBtn, &rect);
    
    Graphics graphics(hdc);
    graphics.SetSmoothingMode(SmoothingModeHighQuality);
    graphics.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
    
    // Clear the background to prevent white corners
    graphics.Clear(Color(255, 45, 45, 45)); // Card background color
    
    // Button colors as requested: (68,68,68) base color
    Color bgColor, borderColor, textColor, shadowColor;
    
    if (isPressed) {
        // Darker when pressed
        bgColor = Color(255, 58, 58, 58);
        borderColor = Color(255, 80, 80, 80);
        textColor = Color(255, 220, 220, 220);
        shadowColor = Color(60, 0, 0, 0);
    } else if (isHovered) {
        // Lighter on hover
        bgColor = Color(255, 78, 78, 78);
        borderColor = Color(255, 100, 100, 100);
        textColor = Color(255, 255, 255, 255);
        shadowColor = Color(50, 0, 0, 0);
    } else {
        // Default: (68,68,68) as requested
        bgColor = Color(255, 68, 68, 68);
        borderColor = Color(255, 85, 85, 85);
        textColor = Color(255, 230, 230, 230);
        shadowColor = Color(30, 0, 0, 0);
    }
    
    // Flat design - no gradient, just solid color
    SolidBrush bgBrush(bgColor);
    
    // Draw rounded rectangle with smaller radius for modern flat look
    GraphicsPath path;
    int radius = 4;  // Smaller radius for flatter look
    path.AddArc(0, 0, radius * 2, radius * 2, 180, 90);
    path.AddArc(rect.right - radius * 2, 0, radius * 2, radius * 2, 270, 90);
    path.AddArc(rect.right - radius * 2, rect.bottom - radius * 2, radius * 2, radius * 2, 0, 90);
    path.AddArc(0, rect.bottom - radius * 2, radius * 2, radius * 2, 90, 90);
    path.CloseFigure();
    
    // Only very subtle shadow for depth, no shadow when pressed
    if (!isPressed) {
        GraphicsPath shadowPath;
        int shadowOffset = 1;  // Much smaller shadow
        shadowPath.AddArc(shadowOffset, shadowOffset, radius * 2, radius * 2, 180, 90);
        shadowPath.AddArc(rect.right - radius * 2 + shadowOffset, shadowOffset, radius * 2, radius * 2, 270, 90);
        shadowPath.AddArc(rect.right - radius * 2 + shadowOffset, rect.bottom - radius * 2 + shadowOffset, radius * 2, radius * 2, 0, 90);
        shadowPath.AddArc(shadowOffset, rect.bottom - radius * 2 + shadowOffset, radius * 2, radius * 2, 90, 90);
        shadowPath.CloseFigure();
        
        SolidBrush shadowBrush(shadowColor);
        graphics.FillPath(&shadowBrush, &shadowPath);
    }
    
    graphics.FillPath(&bgBrush, &path);
    
    // Draw very subtle border
    Pen borderPen(borderColor, 0.5f);  // Thinner border for flat design
    graphics.DrawPath(&borderPen, &path);
    
    // Draw text with system font (matching HTML font-family)
    FontFamily fontFamily(L"Segoe UI");
    Font font(&fontFamily, 12, FontStyleRegular, UnitPoint);
    SolidBrush textBrush(textColor);
    
    RectF textRect((REAL)rect.left, (REAL)rect.top, (REAL)(rect.right - rect.left), (REAL)(rect.bottom - rect.top));
    StringFormat stringFormat;
    stringFormat.SetAlignment(StringAlignmentCenter);
    stringFormat.SetLineAlignment(StringAlignmentCenter);
    
    graphics.DrawString(text.c_str(), -1, &font, textRect, &stringFormat, &textBrush);
}

bool Application::IsPointInButton(HWND hBtn, POINT pt) {
    RECT btnRect;
    GetWindowRect(hBtn, &btnRect);
    ScreenToClient(m_hWnd, (LPPOINT)&btnRect.left);
    ScreenToClient(m_hWnd, (LPPOINT)&btnRect.right);
    return PtInRect(&btnRect, pt);
}

void Application::InvalidateButton(HWND hBtn) {
    InvalidateRect(hBtn, nullptr, FALSE);
    UpdateWindow(hBtn);
}


void Application::DrawBackground(HDC hdc, RECT rect) {
    // Fill with main background color --bg-color: #1a1a1a
    HBRUSH bgBrush = CreateSolidBrush(RGB(26, 26, 26));
    FillRect(hdc, &rect, bgBrush);
    DeleteObject(bgBrush);
}

void Application::DrawCard(HDC hdc, RECT rect, const std::wstring& title) {
    Graphics graphics(hdc);
    graphics.SetSmoothingMode(SmoothingModeHighQuality);
    graphics.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
    
    // Card background color --card-bg: #2d2d2d
    Color cardBg(255, 45, 45, 45);
    Color borderColor(255, 64, 64, 64);  // --border-color: #404040
    
    // Create rounded rectangle path (8px radius like HTML)
    GraphicsPath path;
    int radius = 8;
    path.AddArc(rect.left, rect.top, radius * 2, radius * 2, 180, 90);
    path.AddArc(rect.right - radius * 2, rect.top, radius * 2, radius * 2, 270, 90);
    path.AddArc(rect.right - radius * 2, rect.bottom - radius * 2, radius * 2, radius * 2, 0, 90);
    path.AddArc(rect.left, rect.bottom - radius * 2, radius * 2, radius * 2, 90, 90);
    path.CloseFigure();
    
    // Draw shadow first (--shadow: 0 2px 8px rgba(0,0,0,0.3))
    GraphicsPath shadowPath;
    int shadowOffset = 2;
    shadowPath.AddArc(rect.left + shadowOffset, rect.top + shadowOffset, radius * 2, radius * 2, 180, 90);
    shadowPath.AddArc(rect.right - radius * 2 + shadowOffset, rect.top + shadowOffset, radius * 2, radius * 2, 270, 90);
    shadowPath.AddArc(rect.right - radius * 2 + shadowOffset, rect.bottom - radius * 2 + shadowOffset, radius * 2, radius * 2, 0, 90);
    shadowPath.AddArc(rect.left + shadowOffset, rect.bottom - radius * 2 + shadowOffset, radius * 2, radius * 2, 90, 90);
    shadowPath.CloseFigure();
    
    SolidBrush shadowBrush(Color(76, 0, 0, 0)); // rgba(0,0,0,0.3)
    graphics.FillPath(&shadowBrush, &shadowPath);
    
    // Draw card background
    SolidBrush cardBrush(cardBg);
    graphics.FillPath(&cardBrush, &path);
    
    // Draw border
    Pen borderPen(borderColor, 1.0f);
    graphics.DrawPath(&borderPen, &path);
    
    // Draw title if provided
    if (!title.empty()) {
        FontFamily fontFamily(L"Segoe UI");
        Font font(&fontFamily, 12, FontStyleBold, UnitPoint); // Bold font for better visibility
        SolidBrush textBrush(Color(255, 255, 255, 255));     // White text for better contrast
        
        RectF titleRect((REAL)rect.left + 16, (REAL)rect.top + 6, (REAL)(rect.right - rect.left - 32), 24);
        StringFormat stringFormat;
        stringFormat.SetAlignment(StringAlignmentNear);
        stringFormat.SetLineAlignment(StringAlignmentCenter);
        
        graphics.DrawString(title.c_str(), -1, &font, titleRect, &stringFormat, &textBrush);
    }
}

void Application::DrawEditBorder(HWND hEdit) {
    if (!hEdit) return;
    
    RECT rect;
    GetWindowRect(hEdit, &rect);
    ScreenToClient(m_hWnd, (LPPOINT)&rect.left);
    ScreenToClient(m_hWnd, (LPPOINT)&rect.right);
    
    HDC hdc = GetDC(m_hWnd);
    
    // Draw dark border around edit control
    HPEN hPen = CreatePen(PS_SOLID, 1, RGB(64, 64, 64)); // Dark gray border
    HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);
    
    // Draw border rectangle
    MoveToEx(hdc, rect.left - 1, rect.top - 1, nullptr);
    LineTo(hdc, rect.right, rect.top - 1);
    LineTo(hdc, rect.right, rect.bottom);
    LineTo(hdc, rect.left - 1, rect.bottom);
    LineTo(hdc, rect.left - 1, rect.top - 1);
    
    SelectObject(hdc, hOldPen);
    DeleteObject(hPen);
    ReleaseDC(m_hWnd, hdc);
}

LRESULT CALLBACK Application::TreeCanvasSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
    Application* pApp = (Application*)dwRefData;
    
    switch (uMsg) {
    case WM_MOUSEWHEEL:
        {
            int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            int scrollLines = 3;
            
            if (delta > 0) {
                // Scroll up
                for (int i = 0; i < scrollLines; i++) {
                    SendMessage(hWnd, WM_VSCROLL, SB_LINEUP, 0);
                }
            } else {
                // Scroll down
                for (int i = 0; i < scrollLines; i++) {
                    SendMessage(hWnd, WM_VSCROLL, SB_LINEDOWN, 0);
                }
            }
            return 0;
        }
    }
    
    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}