#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN

#include "thumbnail_provider.h"
#include <windows.h>
#include <shlobj.h>
#include <string>
#include <vector>
#include <new>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "uuid.lib")
#pragma comment(lib, "crypt32.lib")

namespace PsdThumbnail {
    std::atomic<LONG> g_dllRefCount(0);
}

static HINSTANCE g_hInstance = NULL;

static const wchar_t CLSID_OUR[] = L"{66FC65C5-2883-4D88-82AF-70626AF90EE8}";
static const wchar_t IID_THUMB[] = L"{e357fccd-a995-4576-b01f-234630154e96}";
static const wchar_t FRIENDLY_NAME[] = L"PSD Thumbnail Provider Shell Extension";
static const wchar_t BACKUP_BASE[] = L"Software\\PsdThumbnailProvider\\Backup";

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
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID /*lpReserved*/) {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        g_hInstance = hModule;
        DisableThreadLibraryCalls(hModule);
    }
    return TRUE;
}

// ── Exported Functions ────────────────────────────────────────────────────
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdll-attribute-on-redeclaration"
#endif

extern "C" __declspec(dllexport) HRESULT __stdcall DllCanUnloadNow(void) {
    return (PsdThumbnail::g_dllRefCount.load() == 0) ? S_OK : S_FALSE;
}

extern "C" __declspec(dllexport) HRESULT __stdcall DllGetClassObject(REFCLSID rclsid, REFIID riid, LPVOID* ppv) {
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
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

// ── Registry Helpers (Wide APIs, R-01 & R-02) ──────────────────────────────
static std::wstring GetCurrentModulePath(HMODULE hMod) {
    std::wstring path(MAX_PATH, L'\0');
    for (;;) {
        DWORD n = GetModuleFileNameW(hMod, path.data(), static_cast<DWORD>(path.size()));
        if (n == 0) return L"";
        if (n < path.size()) {
            path.resize(n);
            return path;
        }
        path.resize(path.size() * 2);
    }
}

static LONG ReadRegString(HKEY root, const std::wstring& subKey, const wchar_t* valName, std::wstring& outVal) {
    HKEY hKey = NULL;
    LONG res = RegOpenKeyExW(root, subKey.c_str(), 0, KEY_READ, &hKey);
    if (res != ERROR_SUCCESS) return res;

    wchar_t buf[1024];
    DWORD cbData = sizeof(buf);
    DWORD dwType = 0;
    res = RegQueryValueExW(hKey, valName, NULL, &dwType, reinterpret_cast<BYTE*>(buf), &cbData);
    RegCloseKey(hKey);

    if (res == ERROR_SUCCESS && (dwType == REG_SZ || dwType == REG_EXPAND_SZ)) {
        outVal = buf;
    }
    return res;
}

static LONG SetRegString(HKEY root, const std::wstring& subKey, const wchar_t* valName, const std::wstring& data) {
    HKEY hKey = NULL;
    LONG res = RegCreateKeyExW(root, subKey.c_str(), 0, NULL, REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, NULL);
    if (res == ERROR_SUCCESS) {
        DWORD cbBytes = static_cast<DWORD>((data.length() + 1) * sizeof(wchar_t));
        res = RegSetValueExW(hKey, valName, 0, REG_SZ, reinterpret_cast<const BYTE*>(data.c_str()), cbBytes);
        RegCloseKey(hKey);
    }
    return res;
}

// Register key with backup of previous handler (R-02)
static LONG RegisterShellExWithBackup(HKEY root, const std::wstring& shellExKey, const std::wstring& backupKeyName) {
    std::wstring existing;
    LONG readRes = ReadRegString(root, shellExKey, NULL, existing);

    // If an existing handler from another product exists, back it up first
    if (readRes == ERROR_SUCCESS && !existing.empty() && _wcsicmp(existing.c_str(), CLSID_OUR) != 0) {
        SetRegString(root, std::wstring(BACKUP_BASE), backupKeyName.c_str(), existing);
    }

    return SetRegString(root, shellExKey, NULL, CLSID_OUR);
}

// Unregister key with restore of previous handler if backed up (R-02)
static void UnregisterShellExWithRestore(HKEY root, const std::wstring& shellExKey, const std::wstring& backupKeyName) {
    std::wstring current;
    LONG readRes = ReadRegString(root, shellExKey, NULL, current);

    // Only modify if the current registered handler is OURS!
    if (readRes == ERROR_SUCCESS && _wcsicmp(current.c_str(), CLSID_OUR) == 0) {
        std::wstring backedUp;
        LONG backupRes = ReadRegString(root, std::wstring(BACKUP_BASE), backupKeyName.c_str(), backedUp);
        if (backupRes == ERROR_SUCCESS && !backedUp.empty()) {
            // Restore previous handler
            SetRegString(root, shellExKey, NULL, backedUp);
        } else {
            // Delete key since it was created by us
            RegDeleteKeyW(root, shellExKey.c_str());
        }
    }
}

extern "C" __declspec(dllexport) HRESULT __stdcall DllRegisterServer(void) {
    std::wstring dllPath = GetCurrentModulePath(g_hInstance);
    if (dllPath.empty()) return HRESULT_FROM_WIN32(GetLastError());

    // Choose target root: Try HKEY_LOCAL_MACHINE, fall back to HKEY_CURRENT_USER if access denied
    HKEY hRoot = HKEY_LOCAL_MACHINE;
    std::wstring testKey = std::wstring(L"Software\\Classes\\CLSID\\") + CLSID_OUR;
    HKEY hTest = NULL;
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, testKey.c_str(), 0, NULL, REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hTest, NULL) != ERROR_SUCCESS) {
        hRoot = HKEY_CURRENT_USER;
    } else {
        RegCloseKey(hTest);
    }

    LONG firstError = ERROR_SUCCESS;
    auto check = [&](LONG r) {
        if (r != ERROR_SUCCESS && firstError == ERROR_SUCCESS) firstError = r;
    };

    // 1. CLSID\{GUID}
    std::wstring clsidKey = std::wstring(L"Software\\Classes\\CLSID\\") + CLSID_OUR;
    check(SetRegString(hRoot, clsidKey, NULL, FRIENDLY_NAME));

    // 2. CLSID\{GUID}\InprocServer32
    std::wstring inprocKey = clsidKey + L"\\InprocServer32";
    check(SetRegString(hRoot, inprocKey, NULL, dllPath));
    check(SetRegString(hRoot, inprocKey, L"ThreadingModel", L"Apartment"));

    // 3. Extension associations for .psd, .psb, and .ai with backup
    const wchar_t* exts[] = { L".psd", L".psb", L".ai" };
    for (const wchar_t* ext : exts) {
        // Software\Classes\.ext\ShellEx\{e357fccd...}
        std::wstring k1 = std::wstring(L"Software\\Classes\\") + ext + L"\\ShellEx\\" + IID_THUMB;
        std::wstring b1 = std::wstring(L"Classes_") + (ext + 1);
        check(RegisterShellExWithBackup(hRoot, k1, b1));

        // Software\Classes\SystemFileAssociations\.ext\ShellEx\{e357fccd...}
        std::wstring k2 = std::wstring(L"Software\\Classes\\SystemFileAssociations\\") + ext + L"\\ShellEx\\" + IID_THUMB;
        std::wstring b2 = std::wstring(L"SysAssoc_") + (ext + 1);
        check(RegisterShellExWithBackup(hRoot, k2, b2));
    }

    // 4. Approved list
    std::wstring approvedKey = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Shell Extensions\\Approved";
    check(SetRegString(hRoot, approvedKey, CLSID_OUR, FRIENDLY_NAME));

    if (firstError != ERROR_SUCCESS) {
        return HRESULT_FROM_WIN32(firstError);
    }

    // Broadcast shell change notification
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, NULL, NULL);
    return S_OK;
}

extern "C" __declspec(dllexport) HRESULT __stdcall DllUnregisterServer(void) {
    HKEY roots[] = { HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER };

    for (HKEY hRoot : roots) {
        // 1. Restore or clean extensions
        const wchar_t* exts[] = { L".psd", L".psb", L".ai" };
        for (const wchar_t* ext : exts) {
            std::wstring k1 = std::wstring(L"Software\\Classes\\") + ext + L"\\ShellEx\\" + IID_THUMB;
            std::wstring b1 = std::wstring(L"Classes_") + (ext + 1);
            UnregisterShellExWithRestore(hRoot, k1, b1);

            std::wstring k2 = std::wstring(L"Software\\Classes\\SystemFileAssociations\\") + ext + L"\\ShellEx\\" + IID_THUMB;
            std::wstring b2 = std::wstring(L"SysAssoc_") + (ext + 1);
            UnregisterShellExWithRestore(hRoot, k2, b2);
        }

        // Delete our backup key if empty or done
        RegDeleteTreeW(hRoot, BACKUP_BASE);

        // 2. Delete CLSID key
        std::wstring clsidKey = std::wstring(L"Software\\Classes\\CLSID\\") + CLSID_OUR;
        RegDeleteTreeW(hRoot, clsidKey.c_str());

        // 3. Delete Approved value
        HKEY hApp = NULL;
        if (RegOpenKeyExW(hRoot, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Shell Extensions\\Approved", 0, KEY_SET_VALUE, &hApp) == ERROR_SUCCESS) {
            RegDeleteValueW(hApp, CLSID_OUR);
            RegCloseKey(hApp);
        }
    }

    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, NULL, NULL);
    return S_OK;
}
