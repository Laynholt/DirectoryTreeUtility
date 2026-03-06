#pragma once

#include <windows.h>

#include <string>

class Application;

namespace ApplicationInternal {
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
    ID_INFO_CHECK_UPDATES = 1011,
    ID_MESSAGE_TEXT = 1012,
    ID_MESSAGE_PRIMARY = 1013,
    ID_MESSAGE_SECONDARY = 1014,
    ID_MENU_HELP_HOTKEYS = 2001,
    ID_MENU_HELP_ABOUT = 2002,
    ID_MENU_HELP_SEPARATOR = 2004,
    ID_MENU_CONTEXT_COPY = 2102
};

struct InfoWindowState {
    Application* owner;
    int kind;
    HWND textControl;
    HWND closeButton;
    HWND checkUpdatesButton;
    std::wstring text;
    HFONT font;
    HBRUSH editBrush;
    bool useRichText;
};

struct MessageWindowState {
    Application* owner;
    HWND textControl;
    HWND primaryButton;
    HWND secondaryButton;
    HFONT font;
    HBRUSH editBrush;
    std::wstring text;
    std::wstring primaryButtonText;
    std::wstring secondaryButtonText;
    bool hasSecondaryButton;
    int result;
    int* resultOut;
};

inline constexpr size_t kTreeLargeCharsThreshold = 400000;
inline constexpr size_t kTreeShrinkFactor = 4;
inline constexpr UINT_PTR kTreeCanvasSubclassId = 1;
inline constexpr UINT_PTR kDepthEditSubclassId = 2;
inline constexpr UINT_PTR kTextContextSubclassId = 3;

extern HMODULE g_richEditModule;

const wchar_t* GetHelpMenuItemText(UINT itemId);
const wchar_t* GetMenuItemText(UINT itemId, ULONG_PTR itemData);
void ApplyHotkeysFormatting(HWND richEdit, const std::wstring& text);
} // namespace ApplicationInternal
