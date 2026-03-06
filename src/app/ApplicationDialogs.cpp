#include "Application.h"

#include "AppInfo.h"
#include "AppTheme.h"
#include "ApplicationInternal.h"
#include "UiRenderer.h"

#include <commctrl.h>
#include <richedit.h>

using namespace ApplicationInternal;

HMODULE ApplicationInternal::g_richEditModule = nullptr;

namespace {
void SetControlColors(HDC hdcControl) {
    SetTextColor(hdcControl, AppTheme::kTextPrimary);
    SetBkColor(hdcControl, AppTheme::kCardBackground);
}
} // namespace

const wchar_t* ApplicationInternal::GetHelpMenuItemText(UINT itemId) {
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

const wchar_t* ApplicationInternal::GetMenuItemText(UINT itemId, ULONG_PTR itemData) {
    if (itemData != 0) {
        return reinterpret_cast<const wchar_t*>(itemData);
    }

    return GetHelpMenuItemText(itemId);
}

void ApplicationInternal::ApplyHotkeysFormatting(HWND richEdit, const std::wstring& text) {
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
    SendMessage(richEdit, EM_SETBKGNDCOLOR, FALSE, AppTheme::kCardBackground);

    CHARRANGE allRange = {0, -1};
    SendMessage(richEdit, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&allRange));

    PARAFORMAT2 paragraphFormat = {};
    paragraphFormat.cbSize = sizeof(paragraphFormat);
    paragraphFormat.dwMask = PFM_TABSTOPS;
    paragraphFormat.cTabCount = 1;
    paragraphFormat.rgxTabs[0] = 2600;
    SendMessage(richEdit, EM_SETPARAFORMAT, 0, reinterpret_cast<LPARAM>(&paragraphFormat));

    CHARFORMAT2W baseFormat = {};
    baseFormat.cbSize = sizeof(baseFormat);
    baseFormat.dwMask = CFM_BOLD | CFM_COLOR | CFM_FACE | CFM_SIZE;
    baseFormat.dwEffects = 0;
    baseFormat.crTextColor = AppTheme::kTextPrimary;
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

int Application::ShowStyledMessageDialog(const wchar_t* title,
                                         const std::wstring& bodyText,
                                         const wchar_t* primaryButtonText,
                                         const wchar_t* secondaryButtonText) {
    if (!m_hWnd || !IsWindow(m_hWnd)) {
        return IDCANCEL;
    }

    int result = IDCANCEL;
    auto* state = new MessageWindowState{
        this,
        nullptr,
        nullptr,
        nullptr,
        (m_hInfoFont ? m_hInfoFont : m_hFont),
        nullptr,
        bodyText,
        (primaryButtonText && primaryButtonText[0] != L'\0') ? primaryButtonText : L"Закрыть",
        (secondaryButtonText ? secondaryButtonText : L""),
        (secondaryButtonText && secondaryButtonText[0] != L'\0'),
        IDCANCEL,
        &result
    };

    const int windowWidth = 560;
    const int windowHeight = state->hasSecondaryButton ? 320 : 290;
    RECT parentRect = {};
    GetWindowRect(m_hWnd, &parentRect);
    int x = parentRect.left + ((parentRect.right - parentRect.left) - windowWidth) / 2;
    int y = parentRect.top + ((parentRect.bottom - parentRect.top) - windowHeight) / 2;
    if (x < 0) x = 50;
    if (y < 0) y = 50;

    HWND dialogWindow = CreateWindowEx(
        WS_EX_DLGMODALFRAME,
        AppInfo::kMessageWindowClass,
        (title && title[0] != L'\0') ? title : L"",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        x, y,
        windowWidth, windowHeight,
        m_hWnd,
        nullptr,
        m_hInstance,
        state
    );

    if (!dialogWindow) {
        delete state;
        return IDCANCEL;
    }

    EnableWindow(m_hWnd, FALSE);
    ::ShowWindow(dialogWindow, SW_SHOW);
    UpdateWindow(dialogWindow);
    SetForegroundWindow(dialogWindow);

    MSG msg = {};
    while (IsWindow(dialogWindow)) {
        const BOOL getMessageResult = GetMessage(&msg, nullptr, 0, 0);
        if (getMessageResult == -1) {
            break;
        }
        if (getMessageResult == 0) {
            PostQuitMessage(static_cast<int>(msg.wParam));
            break;
        }

        if (!IsDialogMessage(dialogWindow, &msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    EnableWindow(m_hWnd, TRUE);
    SetForegroundWindow(m_hWnd);
    SetFocus(m_hWnd);
    return result;
}

LRESULT CALLBACK Application::InfoWindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<InfoWindowState*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));

    switch (message) {
    case WM_NCCREATE:
        {
            auto* create = reinterpret_cast<CREATESTRUCT*>(lParam);
            auto* initialState = reinterpret_cast<InfoWindowState*>(create->lpCreateParams);
            SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(initialState));
            return TRUE;
        }

    case WM_CREATE:
        if (state) {
            state->editBrush = CreateSolidBrush(AppTheme::kCardBackground);
            const InfoWindowKind infoKind = static_cast<InfoWindowKind>(state->kind);
            state->useRichText = (infoKind == InfoWindowKind::Hotkeys) && (g_richEditModule != nullptr);

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

            if (infoKind == InfoWindowKind::About) {
                state->checkUpdatesButton = CreateWindowEx(
                    0,
                    L"BUTTON",
                    L"Проверить обновления",
                    WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP,
                    12, 12, 180, 30,
                    hWnd,
                    reinterpret_cast<HMENU>(ID_INFO_CHECK_UPDATES),
                    GetModuleHandle(nullptr),
                    nullptr
                );
            }

            if (state->font) {
                SendMessage(state->textControl, WM_SETFONT, reinterpret_cast<WPARAM>(state->font), MAKELPARAM(FALSE, 0));
                SendMessage(state->closeButton, WM_SETFONT, reinterpret_cast<WPARAM>(state->font), MAKELPARAM(FALSE, 0));
                if (state->checkUpdatesButton) {
                    SendMessage(state->checkUpdatesButton, WM_SETFONT, reinterpret_cast<WPARAM>(state->font), MAKELPARAM(FALSE, 0));
                }
            }

            if (state->owner && state->textControl) {
                SetWindowSubclass(
                    state->textControl,
                    TextEditSubclassProc,
                    kTextContextSubclassId,
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
            const int checkUpdatesWidth = 200;

            MoveWindow(state->textControl, margin, margin, width - (margin * 2), height - (margin * 3) - buttonHeight, TRUE);
            MoveWindow(state->closeButton, width - margin - buttonWidth, height - margin - buttonHeight, buttonWidth, buttonHeight, TRUE);
            if (state->checkUpdatesButton) {
                MoveWindow(state->checkUpdatesButton, margin, height - margin - buttonHeight, checkUpdatesWidth, buttonHeight, TRUE);
            }
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
            SetControlColors(reinterpret_cast<HDC>(wParam));
            return reinterpret_cast<INT_PTR>(state->editBrush);
        }
        break;

    case WM_DRAWITEM:
        {
            auto* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
            if (dis && dis->CtlType == ODT_BUTTON &&
                (dis->CtlID == ID_INFO_CLOSE || dis->CtlID == ID_INFO_CHECK_UPDATES)) {
                wchar_t buttonText[128] = {};
                GetWindowText(dis->hwndItem, buttonText, 128);
                const bool isPressed = (dis->itemState & ODS_SELECTED) != 0;
                const float hoverAlpha = (dis->itemState & ODS_HOTLIGHT) ? 1.0f : 0.0f;
                UiRenderer::DrawCustomButton(dis->hDC, dis->hwndItem, buttonText, isPressed, hoverAlpha);
                return TRUE;
            }
        }
        break;

    case WM_COMMAND:
        if (LOWORD(wParam) == ID_INFO_CHECK_UPDATES && state && state->owner) {
            state->owner->CheckForUpdates();
            return 0;
        }
        if (LOWORD(wParam) == ID_INFO_CLOSE || LOWORD(wParam) == IDOK) {
            DestroyWindow(hWnd);
            return 0;
        }
        break;

    case WM_CLOSE:
        DestroyWindow(hWnd);
        return 0;

    case WM_NCDESTROY:
        if (state) {
            if (state->editBrush) {
                DeleteObject(state->editBrush);
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

LRESULT CALLBACK Application::MessageWindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<MessageWindowState*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));

    switch (message) {
    case WM_NCCREATE:
        {
            auto* create = reinterpret_cast<CREATESTRUCT*>(lParam);
            auto* initialState = reinterpret_cast<MessageWindowState*>(create->lpCreateParams);
            SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(initialState));
            return TRUE;
        }

    case WM_CREATE:
        if (state) {
            state->editBrush = CreateSolidBrush(AppTheme::kCardBackground);
            state->textControl = CreateWindowEx(
                0,
                L"EDIT",
                state->text.c_str(),
                WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_WANTRETURN | ES_READONLY,
                12, 12, 100, 100,
                hWnd,
                reinterpret_cast<HMENU>(ID_MESSAGE_TEXT),
                GetModuleHandle(nullptr),
                nullptr
            );

            state->primaryButton = CreateWindowEx(
                0,
                L"BUTTON",
                state->primaryButtonText.c_str(),
                WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON | BS_OWNERDRAW | WS_TABSTOP,
                12, 12, 120, 32,
                hWnd,
                reinterpret_cast<HMENU>(ID_MESSAGE_PRIMARY),
                GetModuleHandle(nullptr),
                nullptr
            );

            if (state->hasSecondaryButton) {
                state->secondaryButton = CreateWindowEx(
                    0,
                    L"BUTTON",
                    state->secondaryButtonText.c_str(),
                    WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP,
                    12, 12, 120, 32,
                    hWnd,
                    reinterpret_cast<HMENU>(ID_MESSAGE_SECONDARY),
                    GetModuleHandle(nullptr),
                    nullptr
                );
            }

            if (state->font) {
                SendMessage(state->textControl, WM_SETFONT, reinterpret_cast<WPARAM>(state->font), MAKELPARAM(FALSE, 0));
                SendMessage(state->primaryButton, WM_SETFONT, reinterpret_cast<WPARAM>(state->font), MAKELPARAM(FALSE, 0));
                if (state->secondaryButton) {
                    SendMessage(state->secondaryButton, WM_SETFONT, reinterpret_cast<WPARAM>(state->font), MAKELPARAM(FALSE, 0));
                }
            }

            if (state->owner && state->textControl) {
                SetWindowSubclass(
                    state->textControl,
                    TextEditSubclassProc,
                    kTextContextSubclassId,
                    reinterpret_cast<DWORD_PTR>(state->owner)
                );
            }
        }
        return 0;

    case WM_SIZE:
        if (state) {
            const int width = LOWORD(lParam);
            const int height = HIWORD(lParam);
            const int margin = 20;
            const int buttonWidth = 140;
            const int buttonHeight = 32;
            const int buttonGap = 10;

            MoveWindow(state->textControl, margin, margin, width - (margin * 2), height - (margin * 3) - buttonHeight, TRUE);

            const int buttonY = height - margin - buttonHeight;
            if (state->hasSecondaryButton && state->secondaryButton) {
                const int primaryX = width - margin - buttonWidth;
                const int secondaryX = primaryX - buttonGap - buttonWidth;
                MoveWindow(state->secondaryButton, secondaryX, buttonY, buttonWidth, buttonHeight, TRUE);
                MoveWindow(state->primaryButton, primaryX, buttonY, buttonWidth, buttonHeight, TRUE);
            } else {
                MoveWindow(state->primaryButton, width - margin - buttonWidth, buttonY, buttonWidth, buttonHeight, TRUE);
            }
        }
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);

            RECT clientRect = {};
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
            SetControlColors(reinterpret_cast<HDC>(wParam));
            return reinterpret_cast<INT_PTR>(state->editBrush);
        }
        break;

    case WM_DRAWITEM:
        {
            auto* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
            if (dis && dis->CtlType == ODT_BUTTON &&
                (dis->CtlID == ID_MESSAGE_PRIMARY || dis->CtlID == ID_MESSAGE_SECONDARY)) {
                wchar_t buttonText[128] = {};
                GetWindowText(dis->hwndItem, buttonText, 128);
                const bool isPressed = (dis->itemState & ODS_SELECTED) != 0;
                const float hoverAlpha = (dis->itemState & ODS_HOTLIGHT) ? 1.0f : 0.0f;
                UiRenderer::DrawCustomButton(dis->hDC, dis->hwndItem, buttonText, isPressed, hoverAlpha);
                return TRUE;
            }
        }
        break;

    case WM_COMMAND:
        if (state) {
            const UINT commandId = LOWORD(wParam);
            if (commandId == ID_MESSAGE_PRIMARY || commandId == IDOK) {
                state->result = state->hasSecondaryButton ? IDYES : IDOK;
                DestroyWindow(hWnd);
                return 0;
            }
            if (commandId == ID_MESSAGE_SECONDARY || commandId == IDCANCEL) {
                state->result = state->hasSecondaryButton ? IDNO : IDCANCEL;
                DestroyWindow(hWnd);
                return 0;
            }
        }
        break;

    case WM_CLOSE:
        if (state) {
            state->result = state->hasSecondaryButton ? IDNO : IDCANCEL;
        }
        DestroyWindow(hWnd);
        return 0;

    case WM_NCDESTROY:
        if (state) {
            if (state->editBrush) {
                DeleteObject(state->editBrush);
            }
            if (state->resultOut) {
                *state->resultOut = state->result;
            }
            delete state;
            SetWindowLongPtr(hWnd, GWLP_USERDATA, 0);
        }
        return DefWindowProc(hWnd, message, wParam, lParam);
    }

    return DefWindowProc(hWnd, message, wParam, lParam);
}
