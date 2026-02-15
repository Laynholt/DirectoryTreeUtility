#pragma once

#include <windows.h>
#include <shlobj.h>
#include <string>

class FileExplorerIntegration {
public:
    static std::wstring GetActiveExplorerPath();
    static bool IsExplorerWindowActive();
    
private:
    static IShellWindows* GetShellWindows();
    static IWebBrowser2* GetActiveExplorer();
    static std::wstring ExtractPathFromUrl(const std::wstring& url);
};
