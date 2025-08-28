#include "SystemTray.h"
#include "Application.h"
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

    SetWindowLongPtr(m_hTrayWnd, GWLP_USERDATA, (LONG_PTR)this);

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
    m_hIcon = LoadIcon(GetModuleHandle(nullptr), IDI_APPLICATION);
    
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
    
    AppendMenu(m_hContextMenu, MF_STRING, ID_SHOW, L"Показать");
    AppendMenu(m_hContextMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenu(m_hContextMenu, MF_STRING, ID_EXIT, L"Выход");
}

void SystemTray::ShowContextMenu(int x, int y) {
    if (!m_hContextMenu) return;
    
    SetForegroundWindow(m_hTrayWnd);
    
    TrackPopupMenu(m_hContextMenu, TPM_RIGHTBUTTON, x, y, 0, m_hTrayWnd, nullptr);
    
    PostMessage(m_hTrayWnd, WM_NULL, 0, 0);
}

LRESULT CALLBACK SystemTray::TrayWindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    SystemTray* pTray = (SystemTray*)GetWindowLongPtr(hWnd, GWLP_USERDATA);
    
    switch (message) {
    case WM_CREATE:
        {
            CREATESTRUCT* pCreate = (CREATESTRUCT*)lParam;
            pTray = (SystemTray*)pCreate->lpCreateParams;
            SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)pTray);
        }
        break;

    case WM_TRAYICON:
        if (pTray) {
            switch (lParam) {
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