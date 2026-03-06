#pragma once

#include "resource.h"

namespace AppInfo {
inline constexpr wchar_t kProductName[] = L"Directory Tree Utility";
inline constexpr wchar_t kInternalName[] = L"DirectoryTreeUtility";
inline constexpr wchar_t kCompanyName[] = L"laynholt";
inline constexpr wchar_t kMutexName[] = L"DirectoryTreeUtility_SingleInstance";
inline constexpr wchar_t kWindowClass[] = L"DirectoryTreeUtilityClass";
inline constexpr wchar_t kInfoWindowClass[] = L"DirectoryTreeUtilityInfoWindowClass";
inline constexpr wchar_t kMessageWindowClass[] = L"DirectoryTreeUtilityMessageWindowClass";
inline constexpr wchar_t kTrayWindowClass[] = L"DirectoryTreeUtilityTrayClass";
inline constexpr wchar_t kHotkeyWindowClass[] = L"DirectoryTreeUtilityHotkeyClass";
inline constexpr wchar_t kTrayWindowName[] = L"DirectoryTreeUtilityTray";
inline constexpr wchar_t kHotkeyWindowName[] = L"DirectoryTreeUtilityHotkey";
inline constexpr wchar_t kExecutableName[] = L"DirectoryTreeUtility.exe";
inline constexpr wchar_t kTempUpdateExecutableName[] = L"DirectoryTreeUtility_update.exe";
inline constexpr wchar_t kVersion[] = APP_VERSION_DISPLAY_STRING_W;
inline constexpr wchar_t kGitHubHost[] = L"github.com";
inline constexpr wchar_t kGitHubRepoPath[] = L"/Laynholt/DirectoryTreeUtility";
inline constexpr wchar_t kUpdateUserAgent[] = L"DirectoryTreeUtility-Updater/" APP_VERSION_DISPLAY_STRING_W;
} // namespace AppInfo
