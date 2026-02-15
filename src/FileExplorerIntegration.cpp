#include "FileExplorerIntegration.h"
#include <comdef.h>
#include <exdisp.h>
#include <shlguid.h>
#include <shldisp.h>
#include <shlwapi.h>

#pragma comment(lib, "ole32.lib")

namespace {
int HexValue(wchar_t ch) {
    if (ch >= L'0' && ch <= L'9') {
        return static_cast<int>(ch - L'0');
    }
    if (ch >= L'a' && ch <= L'f') {
        return static_cast<int>(10 + (ch - L'a'));
    }
    if (ch >= L'A' && ch <= L'F') {
        return static_cast<int>(10 + (ch - L'A'));
    }
    return -1;
}

std::wstring DecodePercentUtf8(const std::wstring& value) {
    std::string utf8Bytes;
    utf8Bytes.reserve(value.size());

    for (size_t i = 0; i < value.size(); ++i) {
        const wchar_t ch = value[i];
        if (ch == L'%' && i + 2 < value.size()) {
            const int hi = HexValue(value[i + 1]);
            const int lo = HexValue(value[i + 2]);
            if (hi >= 0 && lo >= 0) {
                utf8Bytes.push_back(static_cast<char>((hi << 4) | lo));
                i += 2;
                continue;
            }
        }

        if (ch <= 0x7F) {
            utf8Bytes.push_back(static_cast<char>(ch));
        }
    }

    if (utf8Bytes.empty()) {
        return std::wstring();
    }

    int wideLength = MultiByteToWideChar(CP_UTF8, 0, utf8Bytes.c_str(), static_cast<int>(utf8Bytes.size()), nullptr, 0);
    if (wideLength <= 0) {
        return std::wstring(value);
    }

    std::wstring result(static_cast<size_t>(wideLength), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8Bytes.c_str(), static_cast<int>(utf8Bytes.size()), result.data(), wideLength);
    return result;
}
} // namespace

std::wstring FileExplorerIntegration::GetActiveExplorerPath() {
    // COM should already be initialized by the main application
    
    std::wstring result;
    IWebBrowser2* pActiveExplorer = GetActiveExplorer();
    if (pActiveExplorer) {
        BSTR locationUrl = nullptr;
        
        HRESULT hr = pActiveExplorer->get_LocationURL(&locationUrl);
        if (SUCCEEDED(hr) && locationUrl) {
            result = ExtractPathFromUrl(locationUrl);
        }
        
        if (locationUrl) {
            SysFreeString(locationUrl);
        }
        pActiveExplorer->Release();
    }

    return result;
}

bool FileExplorerIntegration::IsExplorerWindowActive() {
    HWND hWnd = GetForegroundWindow();
    if (!hWnd) return false;
    
    wchar_t className[256];
    GetClassName(hWnd, className, 256);
    
    return wcscmp(className, L"CabinetWClass") == 0 || 
           wcscmp(className, L"ExploreWClass") == 0;
}

IShellWindows* FileExplorerIntegration::GetShellWindows() {
    IShellWindows* pShellWindows = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_ShellWindows, nullptr, CLSCTX_ALL, 
                                 IID_IShellWindows, reinterpret_cast<void**>(&pShellWindows));
    
    return SUCCEEDED(hr) ? pShellWindows : nullptr;
}

IWebBrowser2* FileExplorerIntegration::GetActiveExplorer() {
    IShellWindows* pShellWindows = GetShellWindows();
    if (!pShellWindows) return nullptr;
    
    long windowCount = 0;
    pShellWindows->get_Count(&windowCount);
    
    IWebBrowser2* pActiveExplorer = nullptr;
    HWND hActivWnd = GetForegroundWindow();
    
    for (long i = 0; i < windowCount; ++i) {
        VARIANT index;
        VariantInit(&index);
        index.vt = VT_I4;
        index.lVal = i;
        
        IDispatch* pDispatch = nullptr;
        HRESULT hr = pShellWindows->Item(index, &pDispatch);
        
        if (SUCCEEDED(hr) && pDispatch) {
            IWebBrowser2* pWebBrowser = nullptr;
            hr = pDispatch->QueryInterface(IID_IWebBrowser2, reinterpret_cast<void**>(&pWebBrowser));
            
            if (SUCCEEDED(hr) && pWebBrowser) {
                SHANDLE_PTR hWndPtr = 0;
                hr = pWebBrowser->get_HWND(&hWndPtr);
                
                if (SUCCEEDED(hr)) {
                    HWND hExplorerWnd = reinterpret_cast<HWND>(hWndPtr);
                    
                    if (hExplorerWnd == hActivWnd || IsChild(hExplorerWnd, hActivWnd)) {
                        pActiveExplorer = pWebBrowser;
                        pActiveExplorer->AddRef();
                    }
                }
                
                pWebBrowser->Release();
            }
            
            pDispatch->Release();
        }
        
        VariantClear(&index);
        
        if (pActiveExplorer) break;
    }
    
    if (!pActiveExplorer && windowCount > 0) {
        VARIANT index;
        VariantInit(&index);
        index.vt = VT_I4;
        index.lVal = 0;
        
        IDispatch* pDispatch = nullptr;
        HRESULT hr = pShellWindows->Item(index, &pDispatch);
        
        if (SUCCEEDED(hr) && pDispatch) {
            hr = pDispatch->QueryInterface(IID_IWebBrowser2, reinterpret_cast<void**>(&pActiveExplorer));
            pDispatch->Release();
        }
        
        VariantClear(&index);
    }
    
    pShellWindows->Release();
    return pActiveExplorer;
}

std::wstring FileExplorerIntegration::ExtractPathFromUrl(const std::wstring& url) {
    if (url.empty()) {
        return std::wstring();
    }

    if (url.rfind(L"file:", 0) == 0) {
        DWORD pathSize = static_cast<DWORD>(url.size() + 8);
        std::wstring decodedPath(pathSize, L'\0');
        HRESULT hr = PathCreateFromUrlW(url.c_str(), decodedPath.data(), &pathSize, 0);
        if (SUCCEEDED(hr) && pathSize > 0) {
            decodedPath.resize(pathSize);
            if (!decodedPath.empty() && decodedPath.back() == L'\0') {
                decodedPath.pop_back();
            }
            return decodedPath;
        }

        // Fallback parser for cases when PathCreateFromUrlW fails.
        std::wstring rawPath;
        if (url.rfind(L"file:///", 0) == 0) {
            rawPath = url.substr(8);
        } else if (url.rfind(L"file://", 0) == 0) {
            rawPath = L"\\\\" + url.substr(7); // UNC path
        } else {
            rawPath = url;
        }

        for (wchar_t& ch : rawPath) {
            if (ch == L'/') {
                ch = L'\\';
            }
        }

        std::wstring decodedFallback = DecodePercentUtf8(rawPath);
        if (!decodedFallback.empty()) {
            return decodedFallback;
        }

        return rawPath;
    }
    
    return url;
}
