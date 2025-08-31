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
const UINT WM_ACTIVATE_INSTANCE = WM_USER + 200;

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
    , m_shouldCleanupCanvas(false)
    , m_gdiplusToken(0)
    , m_hoveredButton(nullptr)
    , m_pressedButton(nullptr)
    , m_animationTimer(0)
    , m_hBgBrush(nullptr)
    , m_hEditBrush(nullptr)
    , m_hStaticBrush(nullptr)
    , m_hFont(nullptr)
    , m_hMonoFont(nullptr)
    , m_cancelGeneration(false)
    , m_isGenerating(false)
    , m_animationStep(0) {
    
    // Initialize dark theme brushes
    m_hBgBrush = CreateSolidBrush(RGB(26, 26, 26));      // --bg-color: #1a1a1a
    m_hEditBrush = CreateSolidBrush(RGB(45, 45, 45));    // --card-bg: #2d2d2d
    m_hStaticBrush = CreateSolidBrush(RGB(26, 26, 26));  // Same as background
}

Application::~Application() {
    // Ensure background thread is properly cleaned up
    CancelGeneration();
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

    m_systemTray->AddToTray();
    
    UpdateCurrentPath();

    // Start timer for dynamic path updating (check every 2 seconds)
    SetTimer(m_hWnd, PATH_UPDATE_TIMER_ID, 2000, nullptr);

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

    // Tree canvas positioned inside tree card - scrollbars appear automatically when needed
    m_hTreeCanvas = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                  WS_VISIBLE | WS_CHILD |
                                  ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | ES_AUTOHSCROLL,
                                  24, 176, 740, 300, m_hWnd, reinterpret_cast<HMENU>(ID_TREE_CANVAS), m_hInstance, nullptr);
    
    // Subclass the tree canvas to handle mouse wheel
    SetWindowSubclass(m_hTreeCanvas, TreeCanvasSubclassProc, 1, reinterpret_cast<DWORD_PTR>(this));
    
    // Subclass the depth edit to handle minus key
    SetWindowSubclass(m_hDepthEdit, DepthEditSubclassProc, 2, reinterpret_cast<DWORD_PTR>(this));

    // Status label positioned inside status card
    m_hStatusLabel = CreateWindowEx(0, L"STATIC", L"Готово",
                                   WS_VISIBLE | WS_CHILD,
                                   24, 488, 740, 20, m_hWnd, nullptr, m_hInstance, nullptr);

    // Apply the modern font to all controls
    SendMessage(m_hDepthEdit, WM_SETFONT, reinterpret_cast<WPARAM>(m_hFont), MAKELPARAM(FALSE, 0));
    SendMessage(m_hPathEdit, WM_SETFONT, reinterpret_cast<WPARAM>(m_hFont), MAKELPARAM(FALSE, 0));
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
    
    // Initialize button animation values
    m_buttonHoverAlpha[m_hGenerateBtn] = 0.0f;
    m_buttonHoverAlpha[m_hCopyBtn] = 0.0f;
    m_buttonHoverAlpha[m_hSaveBtn] = 0.0f;
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
        RemoveWindowSubclass(m_hTreeCanvas, TreeCanvasSubclassProc, 1);
    }
    if (m_hDepthEdit) {
        RemoveWindowSubclass(m_hDepthEdit, DepthEditSubclassProc, 2);
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
    
    // Clean up fonts
    if (m_hFont) DeleteObject(m_hFont);
    if (m_hMonoFont) DeleteObject(m_hMonoFont);
    
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
        if (HIWORD(wParam) == EN_SETFOCUS && reinterpret_cast<HWND>(lParam) == m_hDepthEdit) {
            // When depth edit gets focus, select all text for easy replacement
            SendMessage(m_hDepthEdit, EM_SETSEL, 0, -1);
        }
        OnCommand(LOWORD(wParam));
        break;

    case WM_DRAWITEM:
        {
            DRAWITEMSTRUCT* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
            if (dis->CtlType == ODT_BUTTON) {
                std::wstring buttonText;
                wchar_t text[256];
                GetWindowText(dis->hwndItem, text, 256);
                buttonText = text;
                
                bool isPressed = (m_pressedButton == dis->hwndItem) || (dis->itemState & ODS_SELECTED);
                
                DrawCustomButton(dis->hDC, dis->hwndItem, buttonText, isPressed);
                return TRUE;
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
        DrawEditBorder(m_hDepthEdit);
        DrawEditBorder(m_hPathEdit);
        DrawEditBorder(m_hTreeCanvas);
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
            SetWindowText(m_hStatusLabel, L"Готово");
            KillTimer(m_hWnd, STATUS_TIMER_ID);
        }
        else if (wParam == PATH_UPDATE_TIMER_ID) {
            std::wstring currentPath = GetCurrentWorkingPath();
            if (currentPath != m_lastKnownPath) {
                m_lastKnownPath = currentPath;
                SetWindowText(m_hPathEdit, currentPath.c_str());
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
        
    case WM_NCACTIVATE:
        // Ensure title bar is properly activated/deactivated
        return DefWindowProc(m_hWnd, message, wParam, lParam);

    case WM_DESTROY:
        KillTimer(m_hWnd, STATUS_TIMER_ID);
        KillTimer(m_hWnd, PATH_UPDATE_TIMER_ID);
        KillTimer(m_hWnd, PROGRESS_TIMER_ID);
        CancelGeneration(); // Ensure background thread is stopped
        PostQuitMessage(0);
        break;

    case WM_TREE_COMPLETED:
        OnTreeGenerationCompleted(m_treeContent);
        break;
        
    case WM_TREE_ERROR:
        {
            std::wstring* errorMsg = reinterpret_cast<std::wstring*>(lParam);
            OnTreeGenerationError(*errorMsg);
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

void Application::GenerateTree() {
    // Cancel any running generation
    CancelGeneration();
    
    // Set focus to main window
    SetFocus(m_hWnd);
    
    // Start async generation
    GenerateTreeAsync();
}

void Application::GenerateTreeAsync() {
    // Clear previous tree content from memory
    m_treeContent.clear();
    
    // Mark as generating
    m_isGenerating = true;
    m_cancelGeneration = false;
    m_animationStep = 0;
    
    // Start progress animation
    SetTimer(m_hWnd, PROGRESS_TIMER_ID, 500, nullptr);
    
    // Disable generate button
    EnableWindow(m_hGenerateBtn, FALSE);
    
    // Show initial progress message
    SetWindowText(m_hTreeCanvas, L"Генерируется дерево");
    
    // Get generation parameters
    wchar_t depthBuffer[32];
    GetWindowText(m_hDepthEdit, depthBuffer, 32);
    int depth = _wtoi(depthBuffer);
    std::wstring currentPath = GetCurrentWorkingPath();
    
    // Start generation in background thread
    if (m_generationThread.joinable()) {
        m_generationThread.join();
    }
    
    m_generationThread = std::thread([this, currentPath, depth]() {
        try {
            // Generate tree with cancellation support
            std::wstring result = m_treeBuilder->BuildTree(
                currentPath, 
                depth,
                [this]() { return m_cancelGeneration.load(); },
                [this](const std::wstring& progress) {
                    // Post progress update to UI thread
                    UNREFERENCED_PARAMETER(progress);
                    std::lock_guard<std::mutex> lock(m_treeMutex);
                    // We could update progress here if needed
                }
            );
            
            if (!m_cancelGeneration) {
                // Post completion message to UI thread
                std::lock_guard<std::mutex> lock(m_treeMutex);
                
                // Shrink m_treeContent capacity if new tree is significantly smaller (4x smaller)
                if (m_treeContent.capacity() > 4 * result.capacity()) {
                    m_treeContent.shrink_to_fit();
                    m_shouldCleanupCanvas = true; // Flag to cleanup canvas memory
                }
                m_treeContent = result;

                PostMessage(m_hWnd, WM_TREE_COMPLETED, 0, 0);
            }
        }
        catch (const std::exception& e) {
            if (!m_cancelGeneration) {
                // Post error message to UI thread
                std::wstring error = L"Ошибка: ";
                error += std::wstring(e.what(), e.what() + strlen(e.what()));
                PostMessage(m_hWnd, WM_TREE_ERROR, 0, reinterpret_cast<LPARAM>(new std::wstring(error)));
            }
        }
    });
}

void Application::CancelGeneration() {
    if (m_isGenerating) {
        m_cancelGeneration = true;
        
        if (m_generationThread.joinable()) {
            m_generationThread.join();
        }
        
        // Stop progress timer
        KillTimer(m_hWnd, PROGRESS_TIMER_ID);
        
        // Re-enable generate button
        EnableWindow(m_hGenerateBtn, TRUE);
        
        m_isGenerating = false;
    }
}

void Application::OnTreeGenerationCompleted(const std::wstring& result) {
    UNREFERENCED_PARAMETER(result);
    // Stop progress animation
    KillTimer(m_hWnd, PROGRESS_TIMER_ID);
    
    // Cleanup canvas memory if flagged
    if (m_shouldCleanupCanvas) {
        CleanupCanvasMemory();
        m_shouldCleanupCanvas = false; // Reset flag
    }
    
    // Update UI first
    SetWindowText(m_hTreeCanvas, m_treeContent.c_str());
    
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
    ofn.lpstrFilter = L"Text Files (*.txt)\0*.txt\0All Files (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrFileTitle = nullptr;
    ofn.nMaxFileTitle = 0;
    ofn.lpstrInitialDir = nullptr;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT;

    if (GetSaveFileName(&ofn)) {
        std::wstring fileName(szFile);
        
        // Add .txt extension if not present
        if (fileName.length() < 4 || fileName.substr(fileName.length() - 4) != L".txt") {
            fileName += L".txt";
        }
        
        HANDLE hFile = CreateFile(fileName.c_str(), GENERIC_WRITE, 0, nullptr,
                                 CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hFile != INVALID_HANDLE_VALUE) {
            DWORD bytesWritten;
            std::string utf8Content;
            int utf8Length = WideCharToMultiByte(CP_UTF8, 0, m_treeContent.c_str(), -1, nullptr, 0, nullptr, nullptr);
            if (utf8Length > 0) {
                utf8Content.resize(utf8Length - 1);
                WideCharToMultiByte(CP_UTF8, 0, m_treeContent.c_str(), -1, &utf8Content[0], utf8Length, nullptr, nullptr);
            }
            WriteFile(hFile, utf8Content.c_str(), static_cast<DWORD>(utf8Content.length()), &bytesWritten, nullptr);
            CloseHandle(hFile);
            ShowStatusMessage(L"Файл сохранен");
        }
    }
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

void Application::DrawCustomButton(HDC hdc, HWND hBtn, const std::wstring& text, bool isPressed) {
    RECT rect;
    GetClientRect(hBtn, &rect);
    
    // Get animation alpha for smooth hover effect
    float hoverAlpha = m_buttonHoverAlpha[hBtn];
    
    // Create memory DC for double buffering
    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP memBitmap = CreateCompatibleBitmap(hdc, rect.right - rect.left, rect.bottom - rect.top);
    HBITMAP oldBitmap = static_cast<HBITMAP>(SelectObject(memDC, memBitmap));
    
    // Draw to memory DC
    Graphics graphics(memDC);
    graphics.SetSmoothingMode(SmoothingModeHighQuality);
    graphics.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
    graphics.SetPixelOffsetMode(PixelOffsetModeHighQuality);
    
    // Set background to match parent card background exactly
    graphics.Clear(Color(255, 45, 45, 45)); // Card background color

    // Smooth color interpolation based on animation alpha
    Color baseColor = Color(255, 68, 68, 68);      // Default
    Color hoverColor = Color(255, 78, 78, 78);     // Hover
    Color pressedColor = Color(255, 58, 58, 58);   // Pressed
    
    Color baseBorder = Color(255, 85, 85, 85);
    Color hoverBorder = Color(255, 100, 100, 100);
    Color pressedBorder = Color(255, 80, 80, 80);
    
    Color baseText = Color(255, 230, 230, 230);
    Color hoverText = Color(255, 255, 255, 255);
    Color pressedText = Color(255, 220, 220, 220);
    
    
    // Interpolate colors based on hover animation alpha
    Color bgColor, borderColor, textColor;
    if (isPressed) {
        bgColor = pressedColor;
        borderColor = pressedBorder;
        textColor = pressedText;
    } else {
        // Smooth interpolation between base and hover colors
        BYTE bgR = static_cast<BYTE>(baseColor.GetRed() + (hoverColor.GetRed() - baseColor.GetRed()) * hoverAlpha);
        BYTE bgG = static_cast<BYTE>(baseColor.GetGreen() + (hoverColor.GetGreen() - baseColor.GetGreen()) * hoverAlpha);
        BYTE bgB = static_cast<BYTE>(baseColor.GetBlue() + (hoverColor.GetBlue() - baseColor.GetBlue()) * hoverAlpha);
        bgColor = Color(255, bgR, bgG, bgB);
        
        BYTE borderR = static_cast<BYTE>(baseBorder.GetRed() + (hoverBorder.GetRed() - baseBorder.GetRed()) * hoverAlpha);
        BYTE borderG = static_cast<BYTE>(baseBorder.GetGreen() + (hoverBorder.GetGreen() - baseBorder.GetGreen()) * hoverAlpha);
        BYTE borderB = static_cast<BYTE>(baseBorder.GetBlue() + (hoverBorder.GetBlue() - baseBorder.GetBlue()) * hoverAlpha);
        borderColor = Color(255, borderR, borderG, borderB);
        
        BYTE textR = static_cast<BYTE>(baseText.GetRed() + (hoverText.GetRed() - baseText.GetRed()) * hoverAlpha);
        BYTE textG = static_cast<BYTE>(baseText.GetGreen() + (hoverText.GetGreen() - baseText.GetGreen()) * hoverAlpha);
        BYTE textB = static_cast<BYTE>(baseText.GetBlue() + (hoverText.GetBlue() - baseText.GetBlue()) * hoverAlpha);
        textColor = Color(255, textR, textG, textB);
        
    }
    
    // Flat design - no gradient, just solid color
    SolidBrush bgBrush(bgColor);
    
    // Draw rounded rectangle with smaller radius for modern flat look
    float radius = 4.0f;   // Smaller radius for flatter look
    float x = 0.5f;        // Half-pixel offset for crisp borders
    float y = 0.5f;        // Half-pixel offset for crisp borders
    float width = static_cast<float>(rect.right) - 1.0f;
    float height = static_cast<float>(rect.bottom) - 1.0f;

    GraphicsPath path;

    // Top-left corner
    path.AddArc(x, y, radius * 2.0f, radius * 2.0f, 180.0f, 90.0f);
    // Top line
    path.AddLine(x + radius, y, x + width - radius, y);
    // Top-right corner
    path.AddArc(x + width - radius * 2.0f, y, radius * 2.0f, radius * 2.0f, 270.0f, 90.0f);
    // Right line
    path.AddLine(x + width, y + radius, x + width, y + height - radius);
    // Bottom-right corner
    path.AddArc(x + width - radius * 2.0f, y + height - radius * 2.0f, radius * 2.0f, radius * 2.0f, 0.0f, 90.0f);
    // Bottom line
    path.AddLine(x + width - radius, y + height, x + radius, y + height);
    // Bottom-left corner
    path.AddArc(x, y + height - radius * 2.0f, radius * 2.0f, radius * 2.0f, 90.0f, 90.0f);
    // Left line (closes the path)
    path.AddLine(x, y + height - radius, x, y + radius);

    path.CloseFigure();

    
    graphics.FillPath(&bgBrush, &path);

    // Draw very subtle border
    Pen borderPen(borderColor, 1.0f);  // Thinner border for flat design
    graphics.DrawPath(&borderPen, &path);
    
    // Draw text with system font (matching HTML font-family)
    FontFamily fontFamily(L"Segoe UI");
    Font font(&fontFamily, 12, FontStyleRegular, UnitPoint);
    SolidBrush textBrush(textColor);
    
    RectF textRect(static_cast<REAL>(rect.left), static_cast<REAL>(rect.top), static_cast<REAL>(rect.right - rect.left), static_cast<REAL>(rect.bottom - rect.top));
    StringFormat stringFormat;
    stringFormat.SetAlignment(StringAlignmentCenter);
    stringFormat.SetLineAlignment(StringAlignmentCenter);
    
    graphics.DrawString(text.c_str(), -1, &font, textRect, &stringFormat, &textBrush);
    
    // Copy from memory DC to screen DC (double buffering)
    BitBlt(hdc, 0, 0, rect.right - rect.left, rect.bottom - rect.top, memDC, 0, 0, SRCCOPY);
    
    // Clean up memory objects
    SelectObject(memDC, oldBitmap);
    DeleteObject(memBitmap);
    DeleteDC(memDC);
}

bool Application::IsPointInButton(HWND hBtn, POINT pt) {
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
        DrawCustomButton(hdc, hBtn, text, isPressed);
        
        ReleaseDC(hBtn, hdc);
    }
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
        
        RectF titleRect(static_cast<REAL>(rect.left) + 16, static_cast<REAL>(rect.top) + 6, static_cast<REAL>(rect.right - rect.left - 32), 24);
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
    HPEN hOldPen = static_cast<HPEN>(SelectObject(hdc, hPen));
    
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
    UNREFERENCED_PARAMETER(uIdSubclass);
    Application* pApp = reinterpret_cast<Application*>(dwRefData);
    
    switch (uMsg) {
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

void Application::CleanupCanvasMemory() {
    if (!m_hTreeCanvas) return;
    
    // Get current edit control memory handle
    HLOCAL hOrgMem = reinterpret_cast<HLOCAL>(SendMessage(m_hTreeCanvas, EM_GETHANDLE, 0, 0));
    if (!hOrgMem) return;
    
    SIZE_T sizeUsed = LocalSize(hOrgMem);
    
    // Calculate character size (always WCHAR for our case since we use UNICODE)
    int cbCh = sizeof(WCHAR);
    
    // Get current text length
    int textLength = GetWindowTextLength(m_hTreeCanvas);
    
    // Check if we should reduce size of buffer
    if (sizeUsed > m_treeContent.capacity() && (static_cast<SIZE_T>(textLength * cbCh) < m_treeContent.capacity())) {
        // Reallocate memory to smaller size
        HLOCAL hNewMem = reinterpret_cast<HLOCAL>(LocalReAlloc(hOrgMem, m_treeContent.capacity(), LMEM_MOVEABLE));
        if (hNewMem) {
            // Zero full buffer for security
            LPVOID pNewMem = LocalLock(hNewMem);
            if (pNewMem) {
                memset(pNewMem, 0, m_treeContent.capacity());
                LocalUnlock(hNewMem);
            }
            
            // Set new memory handle - reduces process memory
            SendMessage(m_hTreeCanvas, EM_SETHANDLE, reinterpret_cast<WPARAM>(hNewMem), 0);
        }
    }
}

