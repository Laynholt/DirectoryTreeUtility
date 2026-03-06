#include "Application.h"

#include "AppInfo.h"
#include "ApplicationInternal.h"
#include "DarkMode.h"
#include "UiRenderer.h"

#include <windowsx.h>
#include <commctrl.h>
#include <richedit.h>

using namespace ApplicationInternal;

namespace {
std::wstring ReadWindowText(HWND hWnd) {
    const int length = GetWindowTextLengthW(hWnd);
    if (length <= 0) {
        return L"";
    }

    std::wstring buffer(static_cast<size_t>(length) + 1, L'\0');
    GetWindowTextW(hWnd, buffer.data(), length + 1);
    buffer.resize(static_cast<size_t>(length));
    return buffer;
}
} // namespace

void Application::CreateControls() {
    m_hFont = CreateFont(
        -15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI"
    );

    HWND hDepthLabel = CreateWindowEx(0, L"STATIC", L"Глубина дерева:",
        WS_VISIBLE | WS_CHILD, 24, 18, 140, 20, m_hWnd, nullptr, m_hInstance, nullptr);
    HWND hPathLabel = CreateWindowEx(0, L"STATIC", L"Текущий путь:",
        WS_VISIBLE | WS_CHILD, 24, 48, 140, 20, m_hWnd, nullptr, m_hInstance, nullptr);
    UNREFERENCED_PARAMETER(hDepthLabel);
    UNREFERENCED_PARAMETER(hPathLabel);

    m_hDepthEdit = CreateWindowEx(0, L"EDIT", L"1",
        WS_VISIBLE | WS_CHILD | ES_NUMBER,
        174, 16, 80, 24, m_hWnd, reinterpret_cast<HMENU>(ID_DEPTH_EDIT), m_hInstance, nullptr);
    m_hPathEdit = CreateWindowEx(0, L"EDIT", L"",
        WS_VISIBLE | WS_CHILD | ES_READONLY,
        174, 46, 480, 24, m_hWnd, reinterpret_cast<HMENU>(ID_PATH_EDIT), m_hInstance, nullptr);

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

    m_hTreeCanvas = CreateWindowEx(0, L"EDIT", L"",
        WS_VISIBLE | WS_CHILD | WS_VSCROLL |
        ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
        24, 176, 740, 300, m_hWnd, reinterpret_cast<HMENU>(ID_TREE_CANVAS), m_hInstance, nullptr);
    Win32DarkMode::ApplyDarkScrollBarTheme(m_hTreeCanvas);

    SetWindowSubclass(m_hTreeCanvas, TreeCanvasSubclassProc, kTreeCanvasSubclassId, reinterpret_cast<DWORD_PTR>(this));
    SetWindowSubclass(m_hDepthEdit, DepthEditSubclassProc, kDepthEditSubclassId, reinterpret_cast<DWORD_PTR>(this));
    SetWindowSubclass(m_hPathEdit, TextEditSubclassProc, kTextContextSubclassId, reinterpret_cast<DWORD_PTR>(this));

    m_hStatusLabel = CreateWindowEx(0, L"STATIC", L"Готово",
        WS_VISIBLE | WS_CHILD,
        24, 488, 740, 20, m_hWnd, nullptr, m_hInstance, nullptr);

    SendMessage(m_hDepthEdit, WM_SETFONT, reinterpret_cast<WPARAM>(m_hFont), MAKELPARAM(FALSE, 0));
    SendMessage(m_hPathEdit, WM_SETFONT, reinterpret_cast<WPARAM>(m_hFont), MAKELPARAM(FALSE, 0));
    SendMessage(m_hHelpBtn, WM_SETFONT, reinterpret_cast<WPARAM>(m_hFont), MAKELPARAM(FALSE, 0));
    SendMessage(m_hExpandSymlinksCheck, WM_SETFONT, reinterpret_cast<WPARAM>(m_hFont), MAKELPARAM(FALSE, 0));
    SendMessage(m_hStatusLabel, WM_SETFONT, reinterpret_cast<WPARAM>(m_hFont), MAKELPARAM(FALSE, 0));

    EnumChildWindows(m_hWnd, [](HWND hwndChild, LPARAM lParam) -> BOOL {
        wchar_t className[256];
        GetClassName(hwndChild, className, 256);
        if (wcscmp(className, L"Static") == 0) {
            SendMessage(hwndChild, WM_SETFONT, lParam, MAKELPARAM(FALSE, 0));
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(m_hFont));

    m_hMonoFont = CreateFont(
        -15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas"
    );
    SendMessage(m_hTreeCanvas, WM_SETFONT, reinterpret_cast<WPARAM>(m_hMonoFont), MAKELPARAM(FALSE, 0));
    SendMessage(m_hTreeCanvas, EM_SETUNDOLIMIT, 0, 0);
    UpdateTreeCanvasScrollBarVisibility();

    m_hInfoFont = CreateFont(
        -15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI"
    );

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
    AppendMenu(m_hMainMenu, MF_OWNERDRAW, ID_MENU_HELP_SEPARATOR, L"");
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
    wcex.lpszClassName = AppInfo::kInfoWindowClass;
    wcex.hIconSm = LoadIcon(m_hInstance, MAKEINTRESOURCE(IDI_MAIN_ICON));

    if (!RegisterClassEx(&wcex)) {
        return GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
    }

    return true;
}

bool Application::RegisterMessageWindowClass() {
    WNDCLASSEX wcex = {};
    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = MessageWindowProc;
    wcex.hInstance = m_hInstance;
    wcex.hIcon = LoadIcon(m_hInstance, MAKEINTRESOURCE(IDI_MAIN_ICON));
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.lpszClassName = AppInfo::kMessageWindowClass;
    wcex.hIconSm = LoadIcon(m_hInstance, MAKEINTRESOURCE(IDI_MAIN_ICON));

    if (!RegisterClassEx(&wcex)) {
        return GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
    }

    return true;
}

void Application::OnResize(int width, int height) {
    if (width <= 0 || height <= 0) {
        return;
    }

    const int helpButtonWidth = 106;
    const int helpButtonHeight = 24;
    const int helpButtonX = width - 24 - helpButtonWidth;
    MoveWindow(m_hHelpBtn, helpButtonX, 16, helpButtonWidth, helpButtonHeight, TRUE);

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
            checkboxWidth = textSize.cx + 42;

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

    const int totalButtonWidth = 170 + 150 + 150 + 20;
    const int startX = (width - totalButtonWidth) / 2;
    MoveWindow(m_hGenerateBtn, startX, 104, 170, 32, TRUE);
    MoveWindow(m_hCopyBtn, startX + 180, 104, 150, 32, TRUE);
    MoveWindow(m_hSaveBtn, startX + 340, 104, 150, 32, TRUE);

    MoveWindow(m_hTreeCanvas, 24, 176, width - 48, height - 248, TRUE);
    MoveWindow(m_hStatusLabel, 24, height - 40, width - 48, 20, TRUE);
    UpdateTreeCanvasScrollBarVisibility();
    InvalidateRect(m_hWnd, nullptr, TRUE);
}

void Application::OnPaint() {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(m_hWnd, &ps);

    RECT clientRect;
    GetClientRect(m_hWnd, &clientRect);

    UiRenderer::DrawBackground(hdc, clientRect);

    RECT inputCard = {8, 8, clientRect.right - 8, 84};
    UiRenderer::DrawCard(hdc, inputCard);

    RECT buttonCard = {8, 92, clientRect.right - 8, 140};
    UiRenderer::DrawCard(hdc, buttonCard);

    RECT treeCard = {8, 148, clientRect.right - 8, clientRect.bottom - 60};
    UiRenderer::DrawCard(hdc, treeCard, L"Дерево каталогов");

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
        SendMessage(m_hDepthEdit, WM_CHAR, VK_BACK, 0);
        break;
    case VK_OEM_MINUS:
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

void Application::UpdateCurrentPath() {
    std::wstring currentPath = GetCurrentWorkingPath();
    m_lastKnownPath = currentPath;
    SetWindowText(m_hPathEdit, currentPath.c_str());
}

void Application::ShowStatusMessage(const std::wstring& message) {
    SetWindowText(m_hStatusLabel, message.c_str());
    SetTimer(m_hWnd, STATUS_TIMER_ID, 3000, nullptr);
}

void Application::ShowPersistentStatusMessage(const std::wstring& message) {
    SetWindowText(m_hStatusLabel, message.c_str());
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

    const UINT selectedCommand = TrackPopupMenu(
        contextMenu,
        TPM_LEFTALIGN | TPM_TOPALIGN | TPM_LEFTBUTTON | TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY,
        popupPoint.x,
        popupPoint.y,
        0,
        m_hWnd,
        nullptr
    );

    if (selectedCommand == ID_MENU_CONTEXT_COPY && hasText) {
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

    DestroyMenu(contextMenu);
    PostMessage(m_hWnd, WM_NULL, 0, 0);
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
    aboutText += AppInfo::kProductName;
    aboutText += L"\r\nВерсия: ";
    aboutText += AppInfo::kVersion;
    aboutText += L"\r\nАвтор: ";
    aboutText += AppInfo::kCompanyName;
    aboutText += L"\r\n\r\nУтилита для построения символьного дерева директорий.\r\n";
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
        nullptr,
        bodyText,
        (m_hInfoFont ? m_hInfoFont : m_hFont),
        nullptr,
        false
    };

    targetHandle = CreateWindowEx(
        WS_EX_DLGMODALFRAME,
        AppInfo::kInfoWindowClass,
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

void Application::OnLButtonDown(LPARAM lParam) {
    POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};

    if (IsPointInButton(m_hGenerateBtn, pt)) {
        m_pressedButton = m_hGenerateBtn;
    } else if (IsPointInButton(m_hCopyBtn, pt)) {
        m_pressedButton = m_hCopyBtn;
    } else if (IsPointInButton(m_hSaveBtn, pt)) {
        m_pressedButton = m_hSaveBtn;
    } else if (IsPointInButton(m_hHelpBtn, pt)) {
        m_pressedButton = m_hHelpBtn;
    }

    if (m_pressedButton) {
        InvalidateButton(m_pressedButton);
        SetCapture(m_hWnd);
    }
}

void Application::OnLButtonUp(LPARAM lParam) {
    if (!m_pressedButton) {
        return;
    }

    POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
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
    InvalidateRect(hBtn, nullptr, TRUE);
    UpdateWindow(hBtn);

    RECT btnRect;
    GetWindowRect(hBtn, &btnRect);
    ScreenToClient(m_hWnd, reinterpret_cast<LPPOINT>(&btnRect.left));
    ScreenToClient(m_hWnd, reinterpret_cast<LPPOINT>(&btnRect.right));
    InvalidateRect(m_hWnd, &btnRect, FALSE);
}

void Application::RedrawButtonDirect(HWND hBtn) {
    RECT rect;
    GetClientRect(hBtn, &rect);
    ValidateRect(hBtn, &rect);

    HDC hdc = GetDC(hBtn);
    if (!hdc) {
        return;
    }

    wchar_t text[256];
    GetWindowText(hBtn, text, 256);
    const bool isPressed = (m_pressedButton == hBtn);
    UiRenderer::DrawCustomButton(hdc, hBtn, text, isPressed, m_buttonHoverAlpha[hBtn]);
    ReleaseDC(hBtn, hdc);
}

LRESULT CALLBACK Application::TreeCanvasSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR dwRefData) {
    Application* pApp = reinterpret_cast<Application*>(dwRefData);

    if (uMsg == WM_CONTEXTMENU && pApp) {
        pApp->ShowTextContextMenu(hWnd, lParam);
        return 0;
    }
    if (uMsg == WM_MOUSEWHEEL && pApp) {
        pApp->HandleMouseWheelScroll(GET_WHEEL_DELTA_WPARAM(wParam));
        return 0;
    }

    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

LRESULT CALLBACK Application::DepthEditSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR dwRefData) {
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
                wchar_t buffer[32];
                GetWindowText(hWnd, buffer, 32);
                int value = _wtoi(buffer);
                swprintf_s(buffer, 32, L"%d", -value);
                SetWindowText(hWnd, buffer);
                pApp->m_isDefaultDepthValue = false;
                pApp->m_hasGeneratedTree = false;
                SetFocus(pApp->m_hWnd);
                return 0;
            }
            if (ch == VK_BACK) {
                if (GetKeyState(VK_SHIFT) & 0x8000) {
                    SetWindowText(hWnd, L"");
                    pApp->m_isDefaultDepthValue = false;
                    pApp->m_hasGeneratedTree = false;
                    SendMessage(hWnd, EM_SETSEL, 0, -1);
                } else {
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
                SetFocus(pApp->m_hWnd);
                return 0;
            }
            if (ch >= L'0' && ch <= L'9') {
                wchar_t buffer[32];
                GetWindowText(hWnd, buffer, 32);

                if (pApp->m_hasGeneratedTree) {
                    swprintf_s(buffer, 32, L"%c", ch);
                    pApp->m_isDefaultDepthValue = false;
                    pApp->m_hasGeneratedTree = false;
                } else if (pApp->m_isDefaultDepthValue && wcscmp(buffer, L"1") == 0 && ch != L'1') {
                    swprintf_s(buffer, 32, L"%c", ch);
                    pApp->m_isDefaultDepthValue = false;
                } else if (wcslen(buffer) == 0 || (wcslen(buffer) == 1 && buffer[0] == L'0')) {
                    swprintf_s(buffer, 32, L"%c", ch);
                    pApp->m_isDefaultDepthValue = false;
                } else {
                    size_t len = wcslen(buffer);
                    if (len < 31) {
                        buffer[len] = ch;
                        buffer[len + 1] = L'\0';
                    }
                    pApp->m_isDefaultDepthValue = false;
                }

                SetWindowText(hWnd, buffer);
                int len = static_cast<int>(wcslen(buffer));
                SendMessage(hWnd, EM_SETSEL, len, len);
                return 0;
            }
            if (ch >= 32) {
                return 0;
            }
        }
        break;

    case WM_KEYDOWN:
        if (wParam == VK_UP || wParam == VK_DOWN) {
            wchar_t buffer[32];
            GetWindowText(hWnd, buffer, 32);
            int value = _wtoi(buffer);

            if (wParam == VK_UP) {
                if (value >= 50) {
                    return 0;
                }
                ++value;
            } else {
                if (!(value > 1 || value < 0)) {
                    return 0;
                }
                --value;
                if (value == 0) {
                    value = -1;
                }
            }

            swprintf_s(buffer, 32, L"%d", value);
            SetWindowText(hWnd, buffer);
            pApp->m_isDefaultDepthValue = false;
            pApp->m_hasGeneratedTree = false;

            int len = static_cast<int>(wcslen(buffer));
            SendMessage(hWnd, EM_SETSEL, len, len);
            return 0;
        }
        break;
    }

    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

LRESULT CALLBACK Application::TextEditSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR dwRefData) {
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
    case 0:
        SendMessage(m_hTreeCanvas, WM_VSCROLL, SB_LINEUP, 0);
        break;
    case 1:
        SendMessage(m_hTreeCanvas, WM_VSCROLL, SB_LINEDOWN, 0);
        break;
    case 2:
        SendMessage(m_hTreeCanvas, WM_HSCROLL, SB_LINELEFT, 0);
        break;
    case 3:
        SendMessage(m_hTreeCanvas, WM_HSCROLL, SB_LINERIGHT, 0);
        break;
    }
}

std::wstring Application::GetCurrentWorkingPath() {
    std::wstring currentPath;
    if (m_hPathEdit && IsWindow(m_hPathEdit)) {
        currentPath = ReadWindowText(m_hPathEdit);
    }
    if (currentPath.empty()) {
        currentPath = m_lastKnownPath;
    }
    if (currentPath.empty()) {
        wchar_t buffer[MAX_PATH] = {};
        GetCurrentDirectory(MAX_PATH, buffer);
        currentPath = buffer;
    }
    return currentPath;
}

void Application::HandleMouseWheelScroll(int delta) {
    constexpr int scrollLines = 3;
    for (int i = 0; i < scrollLines; ++i) {
        ScrollCanvas(delta > 0 ? 0 : 1);
    }
}

bool Application::IsExpandSymlinksEnabled() const {
    return m_expandSymlinks;
}

void Application::UpdateTreeCanvasScrollBarVisibility() {
    if (!m_hTreeCanvas || !IsWindow(m_hTreeCanvas)) {
        return;
    }

    const LRESULT lineCountResult = SendMessage(m_hTreeCanvas, EM_GETLINECOUNT, 0, 0);
    if (lineCountResult <= 0) {
        ShowScrollBar(m_hTreeCanvas, SB_VERT, FALSE);
        return;
    }

    RECT clientRect = {};
    if (!GetClientRect(m_hTreeCanvas, &clientRect)) {
        return;
    }

    HDC hdc = GetDC(m_hTreeCanvas);
    if (!hdc) {
        return;
    }

    HFONT oldFont = nullptr;
    if (m_hMonoFont) {
        oldFont = static_cast<HFONT>(SelectObject(hdc, m_hMonoFont));
    }

    TEXTMETRIC tm = {};
    const BOOL metricsOk = GetTextMetrics(hdc, &tm);

    if (oldFont) {
        SelectObject(hdc, oldFont);
    }
    ReleaseDC(m_hTreeCanvas, hdc);

    if (!metricsOk || tm.tmHeight <= 0) {
        return;
    }

    const int visibleLineCount = (clientRect.bottom - clientRect.top) / tm.tmHeight;
    const bool needsVerticalScrollBar = visibleLineCount > 0 &&
        static_cast<int>(lineCountResult) > visibleLineCount;
    ShowScrollBar(m_hTreeCanvas, SB_VERT, needsVerticalScrollBar ? TRUE : FALSE);
}
