#include "Application.h"
#include "DirectoryTreeBuilder.h"
#include "SystemTray.h"
#include "GlobalHotkeys.h"
#include "FileExplorerIntegration.h"
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>

#pragma comment(lib, "comctl32.lib")

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
    , m_hScrollV(nullptr)
    , m_hScrollH(nullptr)
    , m_currentDepth(1)
    , m_isMinimized(false)
    , m_isDefaultDepthValue(true) {
}

Application::~Application() {
}

bool Application::Initialize(HINSTANCE hInstance) {
    m_hInstance = hInstance;
    
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
    wcex.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wcex.lpszMenuName = nullptr;
    wcex.lpszClassName = WINDOW_CLASS;
    wcex.hIconSm = LoadIcon(nullptr, IDI_APPLICATION);

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
    HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

    CreateWindowEx(0, L"STATIC", L"Глубина дерева:",
                  WS_VISIBLE | WS_CHILD,
                  10, 10, 120, 20, m_hWnd, nullptr, m_hInstance, nullptr);

    m_hDepthEdit = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"1",
                                 WS_VISIBLE | WS_CHILD | WS_BORDER | ES_NUMBER,
                                 140, 8, 60, 24, m_hWnd, (HMENU)ID_DEPTH_EDIT, m_hInstance, nullptr);

    CreateWindowEx(0, L"STATIC", L"Текущий путь:",
                  WS_VISIBLE | WS_CHILD,
                  10, 40, 120, 20, m_hWnd, nullptr, m_hInstance, nullptr);

    m_hPathEdit = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                WS_VISIBLE | WS_CHILD | WS_BORDER | ES_READONLY,
                                140, 38, 400, 24, m_hWnd, (HMENU)ID_PATH_EDIT, m_hInstance, nullptr);

    m_hGenerateBtn = CreateWindowEx(0, L"BUTTON", L"Построить дерево",
                                   WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                                   10, 75, 120, 30, m_hWnd, (HMENU)ID_GENERATE_BTN, m_hInstance, nullptr);

    m_hCopyBtn = CreateWindowEx(0, L"BUTTON", L"Копировать",
                               WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                               140, 75, 100, 30, m_hWnd, (HMENU)ID_COPY_BTN, m_hInstance, nullptr);

    m_hSaveBtn = CreateWindowEx(0, L"BUTTON", L"Сохранить",
                               WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                               250, 75, 100, 30, m_hWnd, (HMENU)ID_SAVE_BTN, m_hInstance, nullptr);

    m_hTreeCanvas = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                  WS_VISIBLE | WS_CHILD | WS_VSCROLL | WS_HSCROLL |
                                  ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | ES_AUTOHSCROLL,
                                  10, 115, 760, 370, m_hWnd, (HMENU)ID_TREE_CANVAS, m_hInstance, nullptr);

    m_hStatusLabel = CreateWindowEx(0, L"STATIC", L"Готово",
                                   WS_VISIBLE | WS_CHILD,
                                   10, 495, 760, 20, m_hWnd, nullptr, m_hInstance, nullptr);

    SendMessage(m_hDepthEdit, WM_SETFONT, (WPARAM)hFont, MAKELPARAM(FALSE, 0));
    SendMessage(m_hPathEdit, WM_SETFONT, (WPARAM)hFont, MAKELPARAM(FALSE, 0));
    SendMessage(m_hGenerateBtn, WM_SETFONT, (WPARAM)hFont, MAKELPARAM(FALSE, 0));
    SendMessage(m_hCopyBtn, WM_SETFONT, (WPARAM)hFont, MAKELPARAM(FALSE, 0));
    SendMessage(m_hSaveBtn, WM_SETFONT, (WPARAM)hFont, MAKELPARAM(FALSE, 0));
    SendMessage(m_hStatusLabel, WM_SETFONT, (WPARAM)hFont, MAKELPARAM(FALSE, 0));

    HFONT hMonoFont = CreateFont(
        14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        DEFAULT_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas"
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
                else if (msg.wParam == VK_BACK && msg.hwnd != m_hDepthEdit) {
                    // Backspace - redirect to depth edit
                    SetFocus(m_hDepthEdit);
                    HandleBackspace();
                    continue;
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

    case WM_SIZE:
        OnResize(LOWORD(lParam), HIWORD(lParam));
        break;

    case WM_PAINT:
        OnPaint();
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
        MoveWindow(m_hPathEdit, 140, 38, width - 160, 24, TRUE);
        MoveWindow(m_hTreeCanvas, 10, 115, width - 40, height - 165, TRUE);
        MoveWindow(m_hStatusLabel, 10, height - 45, width - 40, 20, TRUE);
    }
}

void Application::OnPaint() {
    PAINTSTRUCT ps;
    BeginPaint(m_hWnd, &ps);
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
    
    // If it's the default value "1" and user types a different digit, replace it
    if (m_isDefaultDepthValue && wcscmp(buffer, L"1") == 0 && digit != '1') {
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
        // Shift+Backspace: replace entire text with "1"
        SetWindowText(m_hDepthEdit, L"1");
        m_isDefaultDepthValue = true;
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
        
        // Position cursor at the end
        int len = static_cast<int>(wcslen(buffer));
        SendMessage(m_hDepthEdit, EM_SETSEL, len, len);
    }
}