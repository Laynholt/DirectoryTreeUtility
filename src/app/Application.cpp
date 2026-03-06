#include "Application.h"

#include "AppInfo.h"
#include "AppTheme.h"
#include "ApplicationInternal.h"
#include "DarkMode.h"
#include "FileExplorerIntegration.h"
#include "FileSaveService.h"
#include "GlobalHotkeys.h"
#include "SystemTray.h"
#include "TreeGenerationService.h"
#include "UiRenderer.h"
#include "UpdateService.h"

#include <windowsx.h>
#include <commctrl.h>
#include <gdiplus.h>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "psapi.lib")

using namespace Gdiplus;
using namespace ApplicationInternal;

namespace {
const UINT WM_ACTIVATE_INSTANCE = WM_USER + 200;
}

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
    , m_isMinimized(false)
    , m_isDefaultDepthValue(true)
    , m_hasGeneratedTree(false)
    , m_forceCloseRequested(false)
    , m_isGenerating(false)
    , m_isSaving(false)
    , m_animationStep(0)
    , m_gdiplusToken(0)
    , m_hoveredButton(nullptr)
    , m_pressedButton(nullptr)
    , m_animationTimer(0)
    , m_hBgBrush(CreateSolidBrush(AppTheme::kBackground))
    , m_hEditBrush(CreateSolidBrush(AppTheme::kCardBackground))
    , m_hStaticBrush(CreateSolidBrush(AppTheme::kBackground))
    , m_hFont(nullptr)
    , m_hMonoFont(nullptr)
    , m_hInfoFont(nullptr) {
}

Application::~Application() {
    CancelGeneration();
    if (m_fileSaveService) {
        m_fileSaveService->Cancel();
    }
}

bool Application::Initialize(HINSTANCE hInstance) {
    m_hInstance = hInstance;

    GdiplusStartupInput gdiplusStartupInput;
    GdiplusStartup(&m_gdiplusToken, &gdiplusStartupInput, nullptr);

    const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hr)) {
        MessageBox(nullptr, L"Ошибка инициализации COM", L"Отладка", MB_OK);
        return false;
    }

    INITCOMMONCONTROLSEX icex = {};
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_WIN95_CLASSES;
    InitCommonControlsEx(&icex);

    if (!g_richEditModule) {
        g_richEditModule = LoadLibrary(L"Msftedit.dll");
    }

    Win32DarkMode::Initialize();

    WNDCLASSEX wcex = {};
    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wcex.lpfnWndProc = WindowProc;
    wcex.hInstance = hInstance;
    wcex.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_MAIN_ICON));
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground = m_hBgBrush;
    wcex.lpszClassName = AppInfo::kWindowClass;
    wcex.hIconSm = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_MAIN_ICON));

    if (!RegisterClassEx(&wcex)) {
        MessageBox(nullptr, L"Ошибка регистрации класса окна", L"Отладка", MB_OK);
        return false;
    }

    const int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    const int screenHeight = GetSystemMetrics(SM_CYSCREEN);
    const int windowWidth = 800;
    const int windowHeight = 600;
    const int x = (screenWidth - windowWidth) / 2;
    const int y = (screenHeight - windowHeight) / 2;

    m_hWnd = CreateWindowEx(
        0,
        AppInfo::kWindowClass,
        AppInfo::kProductName,
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
    if (!RegisterMessageWindowClass()) {
        MessageBox(nullptr, L"Ошибка регистрации класса диалогового окна", L"Отладка", MB_OK);
        return false;
    }

    m_systemTray = std::make_unique<SystemTray>(this);
    m_globalHotkeys = std::make_unique<GlobalHotkeys>(this);
    m_treeGenerationService = std::make_unique<TreeGenerationService>();
    m_fileSaveService = std::make_unique<FileSaveService>();
    m_updateService = std::make_unique<UpdateService>();

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
    SetTimer(m_hWnd, PATH_UPDATE_TIMER_ID, 500, nullptr);

    return true;
}

int Application::Run() {
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        if ((msg.message == WM_KEYDOWN || msg.message == WM_CHAR) &&
            (msg.hwnd == m_hWnd || IsChild(m_hWnd, msg.hwnd))) {
            if (msg.message == WM_KEYDOWN) {
                if (msg.wParam == VK_ESCAPE) {
                    SetFocus(m_hWnd);
                    continue;
                }
                if (msg.wParam == VK_RETURN) {
                    GenerateTree();
                    continue;
                }
                if (msg.wParam == 'C' && (GetKeyState(VK_CONTROL) & 0x8000)) {
                    CopyToClipboard();
                    continue;
                }
                if (msg.wParam == 'S' && (GetKeyState(VK_CONTROL) & 0x8000)) {
                    SaveToFile();
                    continue;
                }
                if (msg.wParam >= '0' && msg.wParam <= '9' && msg.hwnd != m_hDepthEdit) {
                    SetFocus(m_hDepthEdit);
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
    if (m_hTreeCanvas) {
        RemoveWindowSubclass(m_hTreeCanvas, TreeCanvasSubclassProc, kTreeCanvasSubclassId);
    }
    if (m_hDepthEdit) {
        RemoveWindowSubclass(m_hDepthEdit, DepthEditSubclassProc, kDepthEditSubclassId);
    }
    if (m_hPathEdit) {
        RemoveWindowSubclass(m_hPathEdit, TextEditSubclassProc, kTextContextSubclassId);
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
    m_updateService.reset();

    if (m_hHotkeysWindow && IsWindow(m_hHotkeysWindow)) {
        DestroyWindow(m_hHotkeysWindow);
        m_hHotkeysWindow = nullptr;
    }
    if (m_hAboutWindow && IsWindow(m_hAboutWindow)) {
        DestroyWindow(m_hAboutWindow);
        m_hAboutWindow = nullptr;
    }
    if (m_hMainMenu) {
        DestroyMenu(m_hMainMenu);
        m_hMainMenu = nullptr;
    }

    if (m_animationTimer) {
        KillTimer(m_hWnd, ANIMATION_TIMER_ID);
        m_animationTimer = 0;
    }

    if (m_hFont) DeleteObject(m_hFont);
    if (m_hMonoFont) DeleteObject(m_hMonoFont);
    if (m_hInfoFont) DeleteObject(m_hInfoFont);
    if (m_hBgBrush) DeleteObject(m_hBgBrush);
    if (m_hEditBrush) DeleteObject(m_hEditBrush);
    if (m_hStaticBrush) DeleteObject(m_hStaticBrush);

    if (m_gdiplusToken) {
        GdiplusShutdown(m_gdiplusToken);
    }
    CoUninitialize();

    if (g_richEditModule) {
        FreeLibrary(g_richEditModule);
        g_richEditModule = nullptr;
    }
}

void Application::RequestExit() {
    m_forceCloseRequested = true;
    CancelGeneration();
    if (m_fileSaveService) {
        m_fileSaveService->Cancel();
    }
    if (m_hWnd && IsWindow(m_hWnd)) {
        DestroyWindow(m_hWnd);
    }
}

void Application::ShowWindow(bool show) {
    if (show) {
        if (IsIconic(m_hWnd)) {
            ::ShowWindow(m_hWnd, SW_RESTORE);
        } else {
            ::ShowWindow(m_hWnd, SW_SHOW);
        }
        ::SetForegroundWindow(m_hWnd);
        m_isMinimized = false;
        return;
    }

    ::ShowWindow(m_hWnd, SW_HIDE);
    m_isMinimized = true;
}

void Application::ToggleVisibility() {
    if (m_isMinimized || !IsWindowVisible(m_hWnd) || IsIconic(m_hWnd)) {
        ShowWindow(true);
    } else if (GetForegroundWindow() == m_hWnd) {
        ShowWindow(false);
    } else {
        SetForegroundWindow(m_hWnd);
        BringWindowToTop(m_hWnd);
        SetActiveWindow(m_hWnd);
    }
}

bool Application::ShouldCloseToTray() const {
    return !m_forceCloseRequested;
}

LRESULT CALLBACK Application::WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    Application* pApp = nullptr;

    if (message == WM_NCCREATE) {
        auto* pCreate = reinterpret_cast<CREATESTRUCT*>(lParam);
        pApp = static_cast<Application*>(pCreate->lpCreateParams);
        SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pApp));
        return DefWindowProc(hWnd, message, wParam, lParam);
    }

    pApp = reinterpret_cast<Application*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));
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
            auto* mis = reinterpret_cast<MEASUREITEMSTRUCT*>(lParam);
            if (mis && mis->CtlType == ODT_MENU) {
                if (mis->itemID == ID_MENU_HELP_SEPARATOR) {
                    mis->itemWidth = 60;
                    mis->itemHeight = 10;
                    return TRUE;
                }

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
            auto* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
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

                wchar_t text[256];
                GetWindowText(dis->hwndItem, text, 256);
                const bool isPressed = (m_pressedButton == dis->hwndItem) || (dis->itemState & ODS_SELECTED);
                UiRenderer::DrawCustomButton(dis->hDC, dis->hwndItem, text, isPressed, m_buttonHoverAlpha[dis->hwndItem]);
                return TRUE;
            }

            if (dis->CtlType == ODT_MENU) {
                if (dis->itemID == ID_MENU_HELP_SEPARATOR) {
                    HBRUSH hBgBrush = CreateSolidBrush(AppTheme::kCardBackground);
                    FillRect(dis->hDC, &dis->rcItem, hBgBrush);
                    DeleteObject(hBgBrush);

                    HPEN hPen = CreatePen(PS_SOLID, 1, RGB(95, 95, 95));
                    HPEN hOldPen = static_cast<HPEN>(SelectObject(dis->hDC, hPen));
                    const int y = dis->rcItem.top + ((dis->rcItem.bottom - dis->rcItem.top) / 2);
                    const int padding = 12;
                    MoveToEx(dis->hDC, dis->rcItem.left + padding, y, nullptr);
                    LineTo(dis->hDC, dis->rcItem.right - padding, y);
                    SelectObject(dis->hDC, hOldPen);
                    DeleteObject(hPen);
                    return TRUE;
                }

                const wchar_t* text = GetMenuItemText(dis->itemID, dis->itemData);
                if (text) {
                    const bool isSelected = (dis->itemState & ODS_SELECTED) != 0;
                    const bool isDisabled = (dis->itemState & ODS_DISABLED) != 0;

                    HBRUSH hBgBrush = CreateSolidBrush(isSelected ? AppTheme::kCardHover : AppTheme::kCardBackground);
                    FillRect(dis->hDC, &dis->rcItem, hBgBrush);
                    DeleteObject(hBgBrush);

                    if (isSelected) {
                        HPEN hPen = CreatePen(PS_SOLID, 1, AppTheme::kCardBorderStrong);
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
                    SetTextColor(dis->hDC, isDisabled ? AppTheme::kTextDisabled : AppTheme::kTextPrimary);

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
        if (LOWORD(lParam) == HTCLIENT) {
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(m_hWnd, &pt);

            HWND previousHovered = m_hoveredButton;
            m_hoveredButton = nullptr;
            if (IsPointInButton(m_hGenerateBtn, pt)) {
                m_hoveredButton = m_hGenerateBtn;
            } else if (IsPointInButton(m_hCopyBtn, pt)) {
                m_hoveredButton = m_hCopyBtn;
            } else if (IsPointInButton(m_hSaveBtn, pt)) {
                m_hoveredButton = m_hSaveBtn;
            } else if (IsPointInButton(m_hHelpBtn, pt)) {
                m_hoveredButton = m_hHelpBtn;
            }

            if (previousHovered != m_hoveredButton && !m_animationTimer) {
                m_animationTimer = SetTimer(m_hWnd, ANIMATION_TIMER_ID, 8, nullptr);
            }

            HWND childWnd = ChildWindowFromPoint(m_hWnd, pt);
            if (childWnd && childWnd != m_hWnd) {
                wchar_t className[256];
                GetClassName(childWnd, className, 256);
                if (wcscmp(className, L"Edit") == 0) {
                    LONG style = GetWindowLong(childWnd, GWL_STYLE);
                    SetCursor(LoadCursor(nullptr, (style & ES_READONLY) ? IDC_ARROW : IDC_IBEAM));
                } else {
                    SetCursor(LoadCursor(nullptr, IDC_ARROW));
                }
            } else {
                SetCursor(LoadCursor(nullptr, IDC_ARROW));
            }
            return TRUE;
        }
        return DefWindowProc(m_hWnd, message, wParam, lParam);

    case WM_MOUSELEAVE:
        {
            HWND previousHovered = m_hoveredButton;
            m_hoveredButton = nullptr;
            if (previousHovered) {
                InvalidateButton(previousHovered);
            }
        }
        break;

    case WM_CTLCOLORSTATIC:
        {
            HDC hdcStatic = reinterpret_cast<HDC>(wParam);
            SetTextColor(hdcStatic, AppTheme::kTextPrimary);
            SetBkColor(hdcStatic, AppTheme::kCardBackground);
            return reinterpret_cast<INT_PTR>(m_hEditBrush);
        }

    case WM_CTLCOLOREDIT:
        {
            HDC hdcEdit = reinterpret_cast<HDC>(wParam);
            SetTextColor(hdcEdit, AppTheme::kTextPrimary);
            SetBkColor(hdcEdit, AppTheme::kCardBackground);
            return reinterpret_cast<INT_PTR>(m_hEditBrush);
        }

    case WM_SIZE:
        OnResize(LOWORD(lParam), HIWORD(lParam));
        break;

    case WM_PAINT:
        OnPaint();
        UiRenderer::DrawEditBorder(m_hWnd, m_hDepthEdit);
        UiRenderer::DrawEditBorder(m_hWnd, m_hPathEdit);
        UiRenderer::DrawEditBorder(m_hWnd, m_hTreeCanvas);
        break;

    case WM_KEYDOWN:
        OnKeyDown(wParam, lParam);
        break;

    case WM_MOUSEWHEEL:
        {
            POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            ScreenToClient(m_hWnd, &pt);

            RECT canvasRect;
            GetWindowRect(m_hTreeCanvas, &canvasRect);
            ScreenToClient(m_hWnd, reinterpret_cast<LPPOINT>(&canvasRect.left));
            ScreenToClient(m_hWnd, reinterpret_cast<LPPOINT>(&canvasRect.right));

            if (PtInRect(&canvasRect, pt)) {
                HandleMouseWheelScroll(GET_WHEEL_DELTA_WPARAM(wParam));
                return 0;
            }
        }
        break;

    case WM_GETMINMAXINFO:
        {
            auto* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
            mmi->ptMinTrackSize.x = MIN_WIDTH;
            mmi->ptMinTrackSize.y = MIN_HEIGHT;
        }
        break;

    case WM_CLOSE:
        if (ShouldCloseToTray()) {
            ShowWindow(false);
            return 0;
        }
        DestroyWindow(m_hWnd);
        return 0;

    case WM_TIMER:
        if (wParam == STATUS_TIMER_ID) {
            if (!m_isGenerating.load() && !m_isSaving.load()) {
                SetWindowText(m_hStatusLabel, L"Готово");
            }
            KillTimer(m_hWnd, STATUS_TIMER_ID);
        } else if (wParam == PATH_UPDATE_TIMER_ID) {
            std::wstring explorerPath = FileExplorerIntegration::GetActiveExplorerPath();
            if (!explorerPath.empty() && explorerPath != m_lastKnownPath) {
                m_lastKnownPath = explorerPath;
                SetWindowText(m_hPathEdit, explorerPath.c_str());
            }
        } else if (wParam == PROGRESS_TIMER_ID) {
            UpdateProgressAnimation();
        } else if (wParam == ANIMATION_TIMER_ID) {
            for (auto& pair : m_buttonHoverAlpha) {
                HWND button = pair.first;
                float& alpha = pair.second;
                const float targetAlpha = (button == m_hoveredButton) ? 1.0f : 0.0f;
                if (alpha != targetAlpha) {
                    alpha = targetAlpha;
                    RedrawButtonDirect(button);
                }
            }

            KillTimer(m_hWnd, ANIMATION_TIMER_ID);
            m_animationTimer = 0;
        }
        break;

    case WM_DESTROY:
        KillTimer(m_hWnd, STATUS_TIMER_ID);
        KillTimer(m_hWnd, PATH_UPDATE_TIMER_ID);
        KillTimer(m_hWnd, PROGRESS_TIMER_ID);
        CancelGeneration();
        if (m_fileSaveService) {
            m_fileSaveService->Cancel();
        }
        PostQuitMessage(0);
        break;

    case WM_TREE_COMPLETED:
        {
            auto* completedResult = reinterpret_cast<std::wstring*>(lParam);
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
            auto* errorMsg = reinterpret_cast<std::wstring*>(lParam);
            if (errorMsg) {
                OnTreeGenerationError(*errorMsg);
                delete errorMsg;
            }
        }
        break;

    case WM_SAVE_COMPLETED:
        OnSaveCompleted();
        break;

    case WM_SAVE_ERROR:
        {
            auto* errorMsg = reinterpret_cast<std::wstring*>(lParam);
            if (errorMsg) {
                OnSaveError(*errorMsg);
                delete errorMsg;
            }
        }
        break;

    case WM_ACTIVATE_INSTANCE:
        ShowWindow(true);
        break;

    default:
        return DefWindowProc(m_hWnd, message, wParam, lParam);
    }
    return 0;
}
