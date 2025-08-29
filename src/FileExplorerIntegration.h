#pragma once

#include <windows.h>
#include <shlobj.h>
#include <string>
#include <vector>

class FileExplorerIntegration {
public:
    static std::wstring GetActiveExplorerPath();
    static bool IsExplorerWindowActive();
    
private:
    static IShellWindows* GetShellWindows();
    static IWebBrowser2* GetActiveExplorer();
    static std::wstring ExtractPathFromVariant(const VARIANT& var);
};