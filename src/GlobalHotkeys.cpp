#include "GlobalHotkeys.h"
#include "Application.h"

const wchar_t* HOTKEY_WINDOW_CLASS = L"DirectoryTreeUtilityHotkeyClass";

GlobalHotkeys::GlobalHotkeys(Application* app)
    : m_application(app)
    , m_hHotkeyWnd(nullptr) {
}

GlobalHotkeys::~GlobalHotkeys() {
    Cleanup();
}

bool GlobalHotkeys::Initialize() {
    WNDCLASSEX wcex = {};
    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.style = 0;
    wcex.lpfnWndProc = HotkeyWindowProc;
    wcex.cbClsExtra = 0;
    wcex.cbWndExtra = 0;
    wcex.hInstance = GetModuleHandle(nullptr);
    wcex.hIcon = nullptr;
    wcex.hCursor = nullptr;
    wcex.hbrBackground = nullptr;
    wcex.lpszMenuName = nullptr;
    wcex.lpszClassName = HOTKEY_WINDOW_CLASS;
    wcex.hIconSm = nullptr;

    if (!RegisterClassEx(&wcex)) {
        DWORD error = GetLastError();
        if (error != ERROR_CLASS_ALREADY_EXISTS) {
            return false;
        }
    }

    m_hHotkeyWnd = CreateWindow(
        HOTKEY_WINDOW_CLASS,
        L"DirectoryTreeUtilityHotkey",
        0, 0, 0, 0, 0,
        HWND_MESSAGE,
        nullptr,
        GetModuleHandle(nullptr),
        this
    );

    if (!m_hHotkeyWnd) {
        return false;
    }

    SetWindowLongPtr(m_hHotkeyWnd, GWLP_USERDATA, (LONG_PTR)this);

    if (!RegisterHotkey(HOTKEY_ALT_T, MOD_ALT, 'T')) {
        return false;
    }

    return true;
}

void GlobalHotkeys::Cleanup() {
    UnregisterHotkey(HOTKEY_ALT_T);
    
    if (m_hHotkeyWnd) {
        DestroyWindow(m_hHotkeyWnd);
        m_hHotkeyWnd = nullptr;
    }
}

bool GlobalHotkeys::RegisterHotkey(int id, UINT modifiers, UINT vk) {
    return RegisterHotKey(m_hHotkeyWnd, id, modifiers, vk) != 0;
}

void GlobalHotkeys::UnregisterHotkey(int id) {
    UnregisterHotKey(m_hHotkeyWnd, id);
}

LRESULT CALLBACK GlobalHotkeys::HotkeyWindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    GlobalHotkeys* pHotkeys = (GlobalHotkeys*)GetWindowLongPtr(hWnd, GWLP_USERDATA);
    
    switch (message) {
    case WM_CREATE:
        {
            CREATESTRUCT* pCreate = (CREATESTRUCT*)lParam;
            pHotkeys = (GlobalHotkeys*)pCreate->lpCreateParams;
            SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)pHotkeys);
        }
        break;

    case WM_HOTKEY:
        if (pHotkeys && pHotkeys->m_application) {
            switch (wParam) {
            case HOTKEY_ALT_T:
                pHotkeys->m_application->ToggleVisibility();
                break;
            }
        }
        break;

    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    
    return 0;
}