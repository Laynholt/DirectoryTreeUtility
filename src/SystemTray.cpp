#include "SystemTray.h"
#include "Application.h"
#include "resource.h"

#include <windowsx.h>

const wchar_t* TRAY_WINDOW_CLASS = L"DirectoryTreeUtilityTrayClass";

SystemTray::SystemTray(Application* app)
    : m_application(app)
    , m_hTrayWnd(nullptr)
    , m_hContextMenu(nullptr)
    , m_hIcon(nullptr) {
    ZeroMemory(&m_nid, sizeof(NOTIFYICONDATA));
}

SystemTray::~SystemTray() {
    Cleanup();
}

bool SystemTray::Initialize() {
    WNDCLASSEX wcex = {};
    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.style = 0;
    wcex.lpfnWndProc = TrayWindowProc;
    wcex.cbClsExtra = 0;
    wcex.cbWndExtra = 0;
    wcex.hInstance = GetModuleHandle(nullptr);
    wcex.hIcon = nullptr;
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground = nullptr;
    wcex.lpszMenuName = nullptr;
    wcex.lpszClassName = TRAY_WINDOW_CLASS;
    wcex.hIconSm = nullptr;

    if (!RegisterClassEx(&wcex)) {
        DWORD error = GetLastError();
        if (error != ERROR_CLASS_ALREADY_EXISTS) {
            return false;
        }
    }

    m_hTrayWnd = CreateWindow(
        TRAY_WINDOW_CLASS,
        L"DirectoryTreeUtilityTray",
        0, 0, 0, 0, 0,
        HWND_MESSAGE,
        nullptr,
        GetModuleHandle(nullptr),
        this
    );

    if (!m_hTrayWnd) {
        return false;
    }

    SetWindowLongPtr(m_hTrayWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

    CreateTrayIcon();
    CreateContextMenu();

    return true;
}

void SystemTray::Cleanup() {
    RemoveFromTray();
    
    if (m_hContextMenu) {
        DestroyMenu(m_hContextMenu);
        m_hContextMenu = nullptr;
    }
    
    if (m_hIcon) {
        DestroyIcon(m_hIcon);
        m_hIcon = nullptr;
    }
    
    if (m_hTrayWnd) {
        DestroyWindow(m_hTrayWnd);
        m_hTrayWnd = nullptr;
    }
}

void SystemTray::AddToTray() {
    Shell_NotifyIcon(NIM_ADD, &m_nid);
}

void SystemTray::RemoveFromTray() {
    Shell_NotifyIcon(NIM_DELETE, &m_nid);
}

void SystemTray::CreateTrayIcon() {
    m_hIcon = LoadIcon(GetModuleHandle(nullptr), MAKEINTRESOURCE(IDI_MAIN_ICON));
    
    m_nid.cbSize = sizeof(NOTIFYICONDATA);
    m_nid.hWnd = m_hTrayWnd;
    m_nid.uID = TRAY_ID;
    m_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    m_nid.uCallbackMessage = WM_TRAYICON;
    m_nid.hIcon = m_hIcon;
    wcscpy_s(m_nid.szTip, L"Directory Tree Utility");
}

void SystemTray::CreateContextMenu() {
    m_hContextMenu = CreatePopupMenu();
    
    // Create owner-drawn menu items for dark theme
    AppendMenu(m_hContextMenu, MF_OWNERDRAW, ID_SHOW, reinterpret_cast<LPCWSTR>(ID_SHOW));
    AppendMenu(m_hContextMenu, MF_OWNERDRAW | MF_SEPARATOR, ID_SEPARATOR, nullptr); // Custom separator
    AppendMenu(m_hContextMenu, MF_OWNERDRAW, ID_EXIT, reinterpret_cast<LPCWSTR>(ID_EXIT));
}

void SystemTray::ShowContextMenu(int x, int y) {
    if (!m_hContextMenu) return;
    
    SetForegroundWindow(m_hTrayWnd);
    
    // Set menu background color to match our dark theme
    MENUINFO mi = {};
    mi.cbSize = sizeof(MENUINFO);
    mi.fMask = MIM_BACKGROUND;
    mi.hbrBack = CreateSolidBrush(RGB(45, 45, 45)); // Dark background
    SetMenuInfo(m_hContextMenu, &mi);
    
    TrackPopupMenu(m_hContextMenu, TPM_RIGHTBUTTON, x, y, 0, m_hTrayWnd, nullptr);
    
    // Clean up brush
    DeleteObject(mi.hbrBack);
    
    PostMessage(m_hTrayWnd, WM_NULL, 0, 0);
}

LRESULT CALLBACK SystemTray::TrayWindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    SystemTray* pTray = reinterpret_cast<SystemTray*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));
    
    switch (message) {
    case WM_CREATE:
        {
            CREATESTRUCT* pCreate = reinterpret_cast<CREATESTRUCT*>(lParam);
            pTray = static_cast<SystemTray*>(pCreate->lpCreateParams);
            SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pTray));
        }
        break;

    case WM_TRAYICON:
        if (pTray) {
            switch (lParam) {
            case WM_LBUTTONUP:
                // Toggle window visibility on left click
                pTray->m_application->ToggleVisibility();
                break;
                
            case WM_LBUTTONDBLCLK:
                pTray->m_application->ShowWindow(true);
                break;
                
            case WM_RBUTTONUP:
                {
                    POINT pt;
                    GetCursorPos(&pt);
                    pTray->ShowContextMenu(pt.x, pt.y);
                }
                break;
            }
        }
        break;

    case WM_MEASUREITEM:
        if (pTray) {
            MEASUREITEMSTRUCT* mis = reinterpret_cast<MEASUREITEMSTRUCT*>(lParam);
            if (mis->CtlType == ODT_MENU) {
                pTray->MeasureMenuItem(mis);
                return TRUE;
            }
        }
        break;

    case WM_DRAWITEM:
        if (pTray) {
            DRAWITEMSTRUCT* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
            if (dis->CtlType == ODT_MENU) {
                pTray->DrawMenuItem(dis);
                return TRUE;
            }
        }
        break;

    case WM_COMMAND:
        if (pTray) {
            switch (LOWORD(wParam)) {
            case ID_SHOW:
                pTray->m_application->ShowWindow(true);
                break;
                
            case ID_EXIT:
                PostQuitMessage(0);
                break;
            }
        }
        break;

    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    
    return 0;
}

void SystemTray::MeasureMenuItem(MEASUREITEMSTRUCT* mis) {
    HDC hdc = GetDC(nullptr);
    HFONT hFont = CreateFont(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                           CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    HFONT hOldFont = static_cast<HFONT>(SelectObject(hdc, hFont));
    
    // Handle separator
    if (mis->itemID == ID_SEPARATOR) {
        mis->itemWidth = 100;
        mis->itemHeight = 3;
        SelectObject(hdc, hOldFont);
        DeleteObject(hFont);
        ReleaseDC(nullptr, hdc);
        return;
    }
    
    std::wstring text;
    switch (mis->itemID) {
    case ID_SHOW:
        text = L"Показать";
        break;
    case ID_EXIT:
        text = L"Выход";
        break;
    }
    
    SIZE textSize;
    GetTextExtentPoint32(hdc, text.c_str(), static_cast<int>(text.length()), &textSize);
    
    mis->itemWidth = textSize.cx + 40;  // Padding
    mis->itemHeight = (textSize.cy + 8 > 24) ? textSize.cy + 8 : 24;  // Minimum height
    
    SelectObject(hdc, hOldFont);
    DeleteObject(hFont);
    ReleaseDC(nullptr, hdc);
}

void SystemTray::DrawMenuItem(DRAWITEMSTRUCT* dis) {
    // Dark theme colors matching application
    COLORREF bgColor = RGB(45, 45, 45);     // Card background
    COLORREF hoverColor = RGB(68, 68, 68);  // Hover background
    COLORREF textColor = RGB(255, 255, 255); // White text
    
    // Fill background
    HBRUSH hBrush;
    if (dis->itemState & ODS_SELECTED) {
        hBrush = CreateSolidBrush(hoverColor);
    } else {
        hBrush = CreateSolidBrush(bgColor);
    }
    
    FillRect(dis->hDC, &dis->rcItem, hBrush);
    DeleteObject(hBrush);
    
    // Handle separator
    if (dis->itemID == ID_SEPARATOR) {
        // Draw gray separator line
        HPEN hPen = CreatePen(PS_SOLID, 1, RGB(128, 128, 128)); // Gray color
        HPEN hOldPen = static_cast<HPEN>(SelectObject(dis->hDC, hPen));
        
        int y = dis->rcItem.top + (dis->rcItem.bottom - dis->rcItem.top) / 2;
        MoveToEx(dis->hDC, dis->rcItem.left + 10, y, nullptr);
        LineTo(dis->hDC, dis->rcItem.right - 10, y);
        
        SelectObject(dis->hDC, hOldPen);
        DeleteObject(hPen);
        return;
    }
    
    // Draw text
    std::wstring text;
    switch (dis->itemID) {
    case ID_SHOW:
        text = L"Показать";
        break;
    case ID_EXIT:
        text = L"Выход";
        break;
    }
    
    if (!text.empty()) {
        HFONT hFont = CreateFont(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                               DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                               CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
        HFONT hOldFont = static_cast<HFONT>(SelectObject(dis->hDC, hFont));
        
        SetTextColor(dis->hDC, textColor);
        SetBkMode(dis->hDC, TRANSPARENT);
        
        RECT textRect = dis->rcItem;
        textRect.left += 20;  // Left padding
        DrawText(dis->hDC, text.c_str(), -1, &textRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        
        SelectObject(dis->hDC, hOldFont);
        DeleteObject(hFont);
    }
}