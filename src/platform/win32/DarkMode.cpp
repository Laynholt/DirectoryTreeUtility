#include "DarkMode.h"

#include "IatHook.h"

#include <uxtheme.h>

#pragma comment(lib, "uxtheme.lib")

namespace {
enum IMMERSIVE_HC_CACHE_MODE {
    IHCM_USE_CACHED_VALUE,
    IHCM_REFRESH
};

enum PreferredAppMode {
    Default,
    AllowDark,
    ForceDark,
    ForceLight,
    Max
};

using fnShouldAppsUseDarkMode = bool (WINAPI*)();
using fnAllowDarkModeForWindow = bool (WINAPI*)(HWND, bool);
using fnAllowDarkModeForApp = bool (WINAPI*)(bool);
using fnRefreshImmersiveColorPolicyState = void (WINAPI*)();
using fnIsDarkModeAllowedForWindow = bool (WINAPI*)(HWND);
using fnGetIsImmersiveColorUsingHighContrast = bool (WINAPI*)(IMMERSIVE_HC_CACHE_MODE);
using fnOpenNcThemeData = HTHEME (WINAPI*)(HWND, LPCWSTR);
using fnSetPreferredAppMode = PreferredAppMode (WINAPI*)(PreferredAppMode);

fnShouldAppsUseDarkMode g_shouldAppsUseDarkMode = nullptr;
fnAllowDarkModeForWindow g_allowDarkModeForWindow = nullptr;
fnAllowDarkModeForApp g_allowDarkModeForApp = nullptr;
fnRefreshImmersiveColorPolicyState g_refreshImmersiveColorPolicyState = nullptr;
fnIsDarkModeAllowedForWindow g_isDarkModeAllowedForWindow = nullptr;
fnGetIsImmersiveColorUsingHighContrast g_getIsImmersiveColorUsingHighContrast = nullptr;
fnOpenNcThemeData g_openNcThemeData = nullptr;
fnSetPreferredAppMode g_setPreferredAppMode = nullptr;

bool g_initialized = false;
bool g_darkModeSupported = false;

bool IsHighContrast() {
    HIGHCONTRASTW highContrast = {sizeof(highContrast)};
    if (SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(highContrast), &highContrast, FALSE)) {
        return (highContrast.dwFlags & HCF_HIGHCONTRASTON) != 0;
    }
    return false;
}

void AllowDarkModeForApp(bool allow) {
    if (g_allowDarkModeForApp) {
        g_allowDarkModeForApp(allow);
    } else if (g_setPreferredAppMode) {
        g_setPreferredAppMode(allow ? AllowDark : Default);
    }
}

void FixDarkScrollBar() {
    HMODULE hComctl = LoadLibraryExW(L"comctl32.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!hComctl) {
        return;
    }

    auto addr = FindDelayLoadThunkInModule(hComctl, "uxtheme.dll", "OpenNcThemeData");
    if (!addr) {
        return;
    }

    DWORD oldProtect = 0;
    if (!VirtualProtect(addr, sizeof(IMAGE_THUNK_DATA), PAGE_READWRITE, &oldProtect)) {
        return;
    }

    auto MyOpenThemeData = [](HWND hWnd, LPCWSTR classList) -> HTHEME {
        if (wcscmp(classList, L"ScrollBar") == 0) {
            hWnd = nullptr;
            classList = L"Explorer::ScrollBar";
        }
        return g_openNcThemeData ? g_openNcThemeData(hWnd, classList) : nullptr;
    };

    addr->u1.Function = reinterpret_cast<ULONG_PTR>(static_cast<fnOpenNcThemeData>(MyOpenThemeData));
    VirtualProtect(addr, sizeof(IMAGE_THUNK_DATA), oldProtect, &oldProtect);
}
} // namespace

void Win32DarkMode::Initialize() {
    if (g_initialized) {
        return;
    }
    g_initialized = true;

    auto rtlGetNtVersionNumbers = reinterpret_cast<void (WINAPI*)(LPDWORD, LPDWORD, LPDWORD)>(
        GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetNtVersionNumbers"));
    if (!rtlGetNtVersionNumbers) {
        return;
    }

    DWORD major = 0;
    DWORD minor = 0;
    DWORD buildNumber = 0;
    rtlGetNtVersionNumbers(&major, &minor, &buildNumber);
    buildNumber &= ~0xF0000000;
    if (!(major == 10 && minor == 0 && buildNumber >= 17763)) {
        return;
    }

    HMODULE hUxtheme = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!hUxtheme) {
        return;
    }

    g_openNcThemeData = reinterpret_cast<fnOpenNcThemeData>(GetProcAddress(hUxtheme, MAKEINTRESOURCEA(49)));
    g_refreshImmersiveColorPolicyState = reinterpret_cast<fnRefreshImmersiveColorPolicyState>(GetProcAddress(hUxtheme, MAKEINTRESOURCEA(104)));
    g_getIsImmersiveColorUsingHighContrast = reinterpret_cast<fnGetIsImmersiveColorUsingHighContrast>(GetProcAddress(hUxtheme, MAKEINTRESOURCEA(106)));
    g_shouldAppsUseDarkMode = reinterpret_cast<fnShouldAppsUseDarkMode>(GetProcAddress(hUxtheme, MAKEINTRESOURCEA(132)));
    g_allowDarkModeForWindow = reinterpret_cast<fnAllowDarkModeForWindow>(GetProcAddress(hUxtheme, MAKEINTRESOURCEA(133)));
    auto ord135 = GetProcAddress(hUxtheme, MAKEINTRESOURCEA(135));
    if (buildNumber < 18362) {
        g_allowDarkModeForApp = reinterpret_cast<fnAllowDarkModeForApp>(ord135);
    } else {
        g_setPreferredAppMode = reinterpret_cast<fnSetPreferredAppMode>(ord135);
    }
    g_isDarkModeAllowedForWindow = reinterpret_cast<fnIsDarkModeAllowedForWindow>(GetProcAddress(hUxtheme, MAKEINTRESOURCEA(137)));

    if (!g_openNcThemeData || !g_refreshImmersiveColorPolicyState || !g_shouldAppsUseDarkMode ||
        !g_allowDarkModeForWindow || (!g_allowDarkModeForApp && !g_setPreferredAppMode) ||
        !g_isDarkModeAllowedForWindow) {
        return;
    }

    g_darkModeSupported = true;
    AllowDarkModeForApp(true);
    g_refreshImmersiveColorPolicyState();
    FixDarkScrollBar();
}

void Win32DarkMode::ApplyDarkScrollBarTheme(HWND hWnd) {
    if (!hWnd) {
        return;
    }

    if (g_darkModeSupported && !IsHighContrast()) {
        g_allowDarkModeForWindow(hWnd, true);
    }

    SetWindowTheme(hWnd, L"Explorer", nullptr);
    SendMessageW(hWnd, WM_THEMECHANGED, 0, 0);
}
