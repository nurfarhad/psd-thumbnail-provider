#include "thumbnail_provider.h"
#include <windows.h>
#include <shlobj.h>
#include <tchar.h>
#include <string>
#include <new>
#include <cstdio>

namespace PsdThumbnail {
    std::atomic<LONG> g_dllRefCount(0);
}

static HINSTANCE g_hInstance = NULL;

const TCHAR g_szClsid[] = _T("{66FC65C5-2883-4D88-82AF-70626AF90EE8}");
const TCHAR g_szThumbHandlerIid[] = _T("{e357fccd-a995-4576-b01f-234630154e96}");
const TCHAR g_szFriendlyName[] = _T("PSD Thumbnail Provider Shell Extension");

// ── IClassFactory Implementation ──────────────────────────────────────────
class CPsdClassFactory : public IClassFactory {
public:
    CPsdClassFactory() : m_refCount(1) {
        PsdThumbnail::g_dllRefCount.fetch_add(1);
    }
    virtual ~CPsdClassFactory() {
        PsdThumbnail::g_dllRefCount.fetch_sub(1);
    }

    STDMETHODIMP QueryInterface(REFIID riid, void** ppvObject) override {
        if (!ppvObject) return E_POINTER;
        *ppvObject = nullptr;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IClassFactory)) {
            *ppvObject = static_cast<IClassFactory*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    STDMETHODIMP_(ULONG) AddRef() override {
        return static_cast<ULONG>(m_refCount.fetch_add(1) + 1);
    }

    STDMETHODIMP_(ULONG) Release() override {
        ULONG count = static_cast<ULONG>(m_refCount.fetch_sub(1) - 1);
        if (count == 0) delete this;
        return count;
    }

    STDMETHODIMP CreateInstance(IUnknown* pUnkOuter, REFIID riid, void** ppvObject) override {
        if (!ppvObject) return E_POINTER;
        *ppvObject = nullptr;
        if (pUnkOuter) return CLASS_E_NOAGGREGATION;

        PsdThumbnail::CPsdThumbnailProvider* pProv = new (std::nothrow) PsdThumbnail::CPsdThumbnailProvider();
        if (!pProv) return E_OUTOFMEMORY;

        HRESULT hr = pProv->QueryInterface(riid, ppvObject);
        pProv->Release();
        return hr;
    }

    STDMETHODIMP LockServer(BOOL fLock) override {
        if (fLock) PsdThumbnail::g_dllRefCount.fetch_add(1);
        else PsdThumbnail::g_dllRefCount.fetch_sub(1);
        return S_OK;
    }

private:
    std::atomic<LONG> m_refCount;
};

// ── DllMain ───────────────────────────────────────────────────────────────
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        g_hInstance = hModule;
        DisableThreadLibraryCalls(hModule);
    }
    return TRUE;
}

// ── Exported Functions ────────────────────────────────────────────────────
STDAPI DllCanUnloadNow(void) {
    return (PsdThumbnail::g_dllRefCount.load() == 0) ? S_OK : S_FALSE;
}

STDAPI DllGetClassObject(
    REFCLSID rclsid, REFIID riid, LPVOID* ppv) {
    if (!ppv) return E_POINTER;
    *ppv = nullptr;

    if (IsEqualCLSID(rclsid, PsdThumbnail::CLSID_PsdThumbnailProvider)) {
        CPsdClassFactory* pFactory = new (std::nothrow) CPsdClassFactory();
        if (!pFactory) return E_OUTOFMEMORY;
        HRESULT hr = pFactory->QueryInterface(riid, ppv);
        pFactory->Release();
        return hr;
    }
    return CLASS_E_CLASSNOTAVAILABLE;
}

// Helper to set registry key and value
static LONG SetRegKey(HKEY hRoot, const TCHAR* subKey, const TCHAR* valName, const TCHAR* data) {
    HKEY hKey = NULL;
    LONG res = RegCreateKeyEx(hRoot, subKey, 0, NULL, REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, NULL);
    if (res == ERROR_SUCCESS) {
        res = RegSetValueEx(hKey, valName, 0, REG_SZ, 
            reinterpret_cast<const BYTE*>(data), static_cast<DWORD>((_tcslen(data) + 1) * sizeof(TCHAR)));
        RegCloseKey(hKey);
    }
    return res;
}

static LONG DeleteRegKeyTree(HKEY hRoot, const TCHAR* subKey) {
    return RegDeleteTree(hRoot, subKey);
}

extern "C" __declspec(dllexport) HRESULT __stdcall DllRegisterServer(void) {
    TCHAR szDllPath[MAX_PATH];
    if (GetModuleFileName(g_hInstance, szDllPath, MAX_PATH) == 0)
        return HRESULT_FROM_WIN32(GetLastError());

    // Choose target root: Try HKEY_LOCAL_MACHINE, fall back to HKEY_CURRENT_USER if access denied
    HKEY hRoot = HKEY_LOCAL_MACHINE;
    TCHAR testKeyName[128];
    _stprintf_s(testKeyName, _T("Software\\Classes\\CLSID\\%s"), g_szClsid);
    HKEY hTestKey = NULL;
    if (RegCreateKeyEx(HKEY_LOCAL_MACHINE, testKeyName, 0, NULL, REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hTestKey, NULL) != ERROR_SUCCESS) {
        hRoot = HKEY_CURRENT_USER;
    } else {
        RegCloseKey(hTestKey);
    }

    TCHAR keyPath[MAX_PATH];

    // 1. CLSID\{GUID}
    _stprintf_s(keyPath, _T("Software\\Classes\\CLSID\\%s"), g_szClsid);
    SetRegKey(hRoot, keyPath, NULL, g_szFriendlyName);

    // 2. CLSID\{GUID}\InprocServer32
    _stprintf_s(keyPath, _T("Software\\Classes\\CLSID\\%s\\InprocServer32"), g_szClsid);
    SetRegKey(hRoot, keyPath, NULL, szDllPath);
    SetRegKey(hRoot, keyPath, _T("ThreadingModel"), _T("Apartment"));

    // 3. Extension associations for .psd, .psb, and .ai
    const TCHAR* exts[] = { _T(".psd"), _T(".psb"), _T(".ai") };
    for (const TCHAR* ext : exts) {
        // Software\Classes\.ext\ShellEx\{e357fccd...}
        _stprintf_s(keyPath, _T("Software\\Classes\\%s\\ShellEx\\%s"), ext, g_szThumbHandlerIid);
        SetRegKey(hRoot, keyPath, NULL, g_szClsid);

        // Software\Classes\SystemFileAssociations\.ext\ShellEx\{e357fccd...}
        _stprintf_s(keyPath, _T("Software\\Classes\\SystemFileAssociations\\%s\\ShellEx\\%s"), ext, g_szThumbHandlerIid);
        SetRegKey(hRoot, keyPath, NULL, g_szClsid);

        // B-10: Only set PerceivedType=Image for PSD/PSB, not .ai (PDF-based format)
        if (_tcscmp(ext, _T(".ai")) != 0) {
            _stprintf_s(keyPath, _T("Software\\Classes\\%s"), ext);
            SetRegKey(hRoot, keyPath, _T("PerceivedType"), _T("Image"));
        }
    }

    // 4. Approved list (HKLM or HKCU)
    SetRegKey(hRoot, _T("SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Shell Extensions\\Approved"), g_szClsid, g_szFriendlyName);

    // Broadcast change notification
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, NULL, NULL);

    return S_OK;
}

extern "C" __declspec(dllexport) HRESULT __stdcall DllUnregisterServer(void) {
    HKEY roots[] = { HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER };

    for (HKEY hRoot : roots) {
        TCHAR keyPath[MAX_PATH];

        // Delete CLSID key
        _stprintf_s(keyPath, _T("Software\\Classes\\CLSID\\%s"), g_szClsid);
        DeleteRegKeyTree(hRoot, keyPath);

        const TCHAR* exts[] = { _T(".psd"), _T(".psb"), _T(".ai") };
        for (const TCHAR* ext : exts) {
            _stprintf_s(keyPath, _T("Software\\Classes\\%s\\ShellEx\\%s"), ext, g_szThumbHandlerIid);
            RegDeleteKey(hRoot, keyPath);

            _stprintf_s(keyPath, _T("Software\\Classes\\SystemFileAssociations\\%s\\ShellEx\\%s"), ext, g_szThumbHandlerIid);
            RegDeleteKey(hRoot, keyPath);
        }

        // Delete Approved value
        HKEY hAppKey = NULL;
        if (RegOpenKeyEx(hRoot, _T("SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Shell Extensions\\Approved"), 0, KEY_SET_VALUE, &hAppKey) == ERROR_SUCCESS) {
            RegDeleteValue(hAppKey, g_szClsid);
            RegCloseKey(hAppKey);
        }
    }

    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, NULL, NULL);
    return S_OK;
}
