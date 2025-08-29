#include "FileExplorerIntegration.h"
#include <comdef.h>
#include <exdisp.h>
#include <shlguid.h>
#include <shldisp.h>

#pragma comment(lib, "ole32.lib")

std::wstring FileExplorerIntegration::GetActiveExplorerPath() {
    // COM should already be initialized by the main application
    
    std::wstring result;
    IShellWindows* pShellWindows = GetShellWindows();
    if (!pShellWindows) {
        return result;
    }
    
    IWebBrowser2* pActiveExplorer = GetActiveExplorer();
    if (pActiveExplorer) {
        BSTR locationUrl = nullptr;
        
        HRESULT hr = pActiveExplorer->get_LocationURL(&locationUrl);
        if (SUCCEEDED(hr) && locationUrl) {
            VARIANT var;
            VariantInit(&var);
            var.vt = VT_BSTR;
            var.bstrVal = locationUrl;
            result = ExtractPathFromVariant(var);
            VariantClear(&var);
        }
        
        if (locationUrl) {
            SysFreeString(locationUrl);
        }
        pActiveExplorer->Release();
    }
    
    pShellWindows->Release();
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
                                 IID_IShellWindows, (void**)&pShellWindows);
    
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
            hr = pDispatch->QueryInterface(IID_IWebBrowser2, (void**)&pWebBrowser);
            
            if (SUCCEEDED(hr) && pWebBrowser) {
                SHANDLE_PTR hWndPtr = 0;
                hr = pWebBrowser->get_HWND(&hWndPtr);
                
                if (SUCCEEDED(hr)) {
                    HWND hExplorerWnd = (HWND)hWndPtr;
                    
                    if (hExplorerWnd == hActivWnd || IsChild(hActivWnd, hExplorerWnd)) {
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
            hr = pDispatch->QueryInterface(IID_IWebBrowser2, (void**)&pActiveExplorer);
            pDispatch->Release();
        }
        
        VariantClear(&index);
    }
    
    pShellWindows->Release();
    return pActiveExplorer;
}

std::wstring FileExplorerIntegration::ExtractPathFromVariant(const VARIANT& var) {
    if (var.vt != VT_BSTR || !var.bstrVal) {
        return std::wstring();
    }
    
    std::wstring url(var.bstrVal);
    
    if (url.find(L"file:///") == 0) {
        std::wstring path = url.substr(8);
        
        size_t pos = 0;
        while ((pos = path.find(L'/', pos)) != std::wstring::npos) {
            path[pos] = L'\\';
            pos++;
        }
        
        pos = 0;
        while ((pos = path.find(L"%20", pos)) != std::wstring::npos) {
            path.replace(pos, 3, L" ");
            pos++;
        }
        
        return path;
    }
    
    return url;
}