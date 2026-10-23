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

#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <thumbcache.h>
#include <wincodec.h>
#include <iostream>
#include <string>
#include <vector>
#include <chrono>
#include <iomanip>
#include <filesystem>
#include <fstream>
#include "../core/psd_decoder.h"

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "crypt32.lib")

namespace fs = std::filesystem;

static const wchar_t* CLSID_OUR_STR = L"{66FC65C5-2883-4D88-82AF-70626AF90EE8}";
static const wchar_t* IID_THUMB_STR  = L"{e357fccd-a995-4576-b01f-234630154e96}";

// Note: ARDFRY_CLSID is solely referenced for identifying competing or legacy orphaned handlers in diagnostics
static const wchar_t* ARDFRY_CLSID   = L"{43341271-304D-40f0-81BB-EBE341997DF2}";

// Check administrator privileges
bool IsUserAdmin() {
    BOOL isAdmin = FALSE;
    PSID adminGroup = NULL;
    SID_IDENTIFIER_AUTHORITY ntAuth = SECURITY_NT_AUTHORITY;
    if (AllocateAndInitializeSid(&ntAuth, 2, SECURITY_BUILTIN_DOMAIN_RID,
        DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &adminGroup)) {
        CheckTokenMembership(NULL, adminGroup, &isAdmin);
        FreeSid(adminGroup);
    }
    return isAdmin != FALSE;
}

// Registry query helper
bool ReadRegString(HKEY root, const std::wstring& subKey, const wchar_t* valName, std::wstring& outValue) {
    HKEY hKey = NULL;
    LONG res = RegOpenKeyExW(root, subKey.c_str(), 0, KEY_READ, &hKey);
    if (res != ERROR_SUCCESS) return false;

    wchar_t buf[1024];
    DWORD cbData = sizeof(buf);
    DWORD dwType = 0;
    res = RegQueryValueExW(hKey, valName, NULL, &dwType, reinterpret_cast<BYTE*>(buf), &cbData);
    RegCloseKey(hKey);

    if (res == ERROR_SUCCESS && (dwType == REG_SZ || dwType == REG_EXPAND_SZ)) {
        outValue = buf;
        return true;
    }
    return false;
}

// Check PE binary architecture
std::wstring GetBinaryArchitecture(const std::wstring& filePath) {
    HANDLE hFile = CreateFileW(filePath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return L"Unknown (cannot open)";

    IMAGE_DOS_HEADER dosHeader;
    DWORD bytesRead = 0;
    if (!ReadFile(hFile, &dosHeader, sizeof(dosHeader), &bytesRead, NULL) || bytesRead != sizeof(dosHeader) || dosHeader.e_magic != IMAGE_DOS_SIGNATURE) {
        CloseHandle(hFile);
        return L"Not a valid PE";
    }

    SetFilePointer(hFile, dosHeader.e_lfanew, NULL, FILE_BEGIN);
    DWORD ntSig = 0;
    IMAGE_FILE_HEADER fileHeader;
    ReadFile(hFile, &ntSig, sizeof(ntSig), &bytesRead, NULL);
    if (ntSig != IMAGE_NT_SIGNATURE || !ReadFile(hFile, &fileHeader, sizeof(fileHeader), &bytesRead, NULL)) {
        CloseHandle(hFile);
        return L"Not a valid NT PE";
    }
    CloseHandle(hFile);

    if (fileHeader.Machine == IMAGE_FILE_MACHINE_AMD64) return L"x64 (64-bit AMD64)";
    if (fileHeader.Machine == 0xAA64) return L"ARM64";
    if (fileHeader.Machine == IMAGE_FILE_MACHINE_I386) return L"x86 (32-bit)";
    return L"Other (" + std::to_wstring(fileHeader.Machine) + L")";
}

// Inspect status
void InspectRegistry() {
    std::wcout << L"==========================================================\n";
    std::wcout << L"   PSD THUMBNAIL SYSTEM DIAGNOSTICS\n";
    std::wcout << L"==========================================================\n";
    std::wcout << L"Running as Admin: " << (IsUserAdmin() ? L"YES" : L"NO") << L"\n\n";

    // 1. Check registered CLSID
    std::wcout << L"--- [CLSID Registration] ---\n";
    std::wstring ourClsidKey = std::wstring(L"Software\\Classes\\CLSID\\") + CLSID_OUR_STR;
    std::wstring inprocKey = ourClsidKey + L"\\InprocServer32";
    
    std::wstring hklmDll, hkcuDll, threading;
    bool hasHklmClsid = ReadRegString(HKEY_LOCAL_MACHINE, inprocKey, L"", hklmDll);
    bool hasHkcuClsid = ReadRegString(HKEY_CURRENT_USER, inprocKey, L"", hkcuDll);
    
    if (hasHklmClsid) {
        std::wcout << L"  [HKLM] Registered: " << hklmDll << L"\n";
        ReadRegString(HKEY_LOCAL_MACHINE, inprocKey, L"ThreadingModel", threading);
        std::wcout << L"         ThreadingModel: " << threading << L"\n";
        bool exists = fs::exists(hklmDll);
        std::wcout << L"         File exists: " << (exists ? L"YES" : L"NO (MISSING FILE!)") << L"\n";
        if (exists) {
            std::wcout << L"         Architecture: " << GetBinaryArchitecture(hklmDll) << L"\n";
        }
    } else {
        std::wcout << L"  [HKLM] Not registered in HKLM\n";
    }

    if (hasHkcuClsid) {
        std::wcout << L"  [HKCU] Registered: " << hkcuDll << L"\n";
        ReadRegString(HKEY_CURRENT_USER, inprocKey, L"ThreadingModel", threading);
        std::wcout << L"         ThreadingModel: " << threading << L"\n";
        bool exists = fs::exists(hkcuDll);
        std::wcout << L"         File exists: " << (exists ? L"YES" : L"NO (MISSING FILE!)") << L"\n";
        if (exists) {
            std::wcout << L"         Architecture: " << GetBinaryArchitecture(hkcuDll) << L"\n";
        }
    } else {
        std::wcout << L"  [HKCU] Not registered in HKCU\n";
    }

    // 2. Check File Association ShellEx and ProgIDs (X-01)
    std::wcout << L"\n--- [File Associations & ProgIDs] ---\n";
    const std::vector<std::wstring> exts = { L".psd", L".psb", L".ai" };
    for (const auto& ext : exts) {
        std::wcout << L"Extension " << ext << L":\n";

        // Check ProgID
        std::wstring progId;
        if (ReadRegString(HKEY_CLASSES_ROOT, ext, L"", progId)) {
            std::wcout << L"  * Default ProgID: " << progId << L"\n";
        }
        std::wstring userChoice;
        std::wstring ucKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\FileExts\\" + ext + L"\\UserChoice";
        if (ReadRegString(HKEY_CURRENT_USER, ucKey, L"ProgId", userChoice)) {
            std::wcout << L"  * UserChoice ProgID: " << userChoice << L"\n";
        }

        // Check HKCU ShellEx
        std::wstring hkcuKey = L"Software\\Classes\\" + ext + L"\\ShellEx\\" + IID_THUMB_STR;
        std::wstring hkcuVal;
        if (ReadRegString(HKEY_CURRENT_USER, hkcuKey, L"", hkcuVal)) {
            std::wcout << L"  * HKCU Classes\\" << ext << L"\\ShellEx: " << hkcuVal;
            if (_wcsicmp(hkcuVal.c_str(), CLSID_OUR_STR) == 0) {
                std::wcout << L" (Our Provider)\n";
            } else if (_wcsicmp(hkcuVal.c_str(), ARDFRY_CLSID) == 0) {
                std::wcout << L" (Ardfry PSD Codec - competing)\n";
            } else {
                std::wcout << L" (Other handler)\n";
            }
        } else {
            std::wcout << L"  * HKCU Classes\\" << ext << L"\\ShellEx: (not set)\n";
        }

        // Check HKLM ShellEx
        std::wstring hklmKey = L"Software\\Classes\\" + ext + L"\\ShellEx\\" + IID_THUMB_STR;
        std::wstring hklmVal;
        if (ReadRegString(HKEY_LOCAL_MACHINE, hklmKey, L"", hklmVal)) {
            std::wcout << L"  * HKLM Classes\\" << ext << L"\\ShellEx: " << hklmVal;
            if (_wcsicmp(hklmVal.c_str(), CLSID_OUR_STR) == 0) {
                std::wcout << L" (Our Provider)\n";
            } else if (_wcsicmp(hklmVal.c_str(), ARDFRY_CLSID) == 0) {
                std::wcout << L" (Ardfry PSD Codec)\n";
            } else {
                std::wcout << L" (Other handler)\n";
            }
        } else {
            std::wcout << L"  * HKLM Classes\\" << ext << L"\\ShellEx: (not set)\n";
        }

        // Check SystemFileAssociations
        std::wstring sfaKey = L"Software\\Classes\\SystemFileAssociations\\" + ext + L"\\ShellEx\\" + IID_THUMB_STR;
        std::wstring sfaVal;
        if (ReadRegString(HKEY_LOCAL_MACHINE, sfaKey, L"", sfaVal)) {
            std::wcout << L"  * HKLM SystemFileAssociations\\" << ext << L"\\ShellEx: " << sfaVal;
            if (_wcsicmp(sfaVal.c_str(), CLSID_OUR_STR) == 0) {
                std::wcout << L" (Our Provider)\n";
            } else {
                std::wcout << L" (Other)\n";
            }
        }
    }

    // 3. Check Approved List
    std::wcout << L"\n--- [Approved Shell Extensions] ---\n";
    std::wstring appKey = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Shell Extensions\\Approved";
    std::wstring appDesc;
    if (ReadRegString(HKEY_LOCAL_MACHINE, appKey, CLSID_OUR_STR, appDesc)) {
        std::wcout << L"  [HKLM Approved]: " << appDesc << L"\n";
    } else if (ReadRegString(HKEY_CURRENT_USER, appKey, CLSID_OUR_STR, appDesc)) {
        std::wcout << L"  [HKCU Approved]: " << appDesc << L"\n";
    } else {
        std::wcout << L"  Not found in Approved list\n";
    }

    std::wcout << L"==========================================================\n";
}

// Save HBITMAP to 24/32-bit BMP file
bool SaveHBitmapToBmp(HBITMAP hBitmap, const std::wstring& filePath) {
    if (!hBitmap) return false;

    BITMAP bm;
    if (!GetObject(hBitmap, sizeof(bm), &bm)) return false;

    BITMAPINFOHEADER bih = {0};
    bih.biSize = sizeof(BITMAPINFOHEADER);
    bih.biWidth = bm.bmWidth;
    bih.biHeight = bm.bmHeight;
    bih.biPlanes = 1;
    bih.biBitCount = 32;
    bih.biCompression = BI_RGB;

    DWORD dwSize = ((bm.bmWidth * 32 + 31) / 32) * 4 * bm.bmHeight;
    std::vector<BYTE> buf(dwSize);

    HDC hdc = GetDC(NULL);
    if (!GetDIBits(hdc, hBitmap, 0, bm.bmHeight, buf.data(), reinterpret_cast<BITMAPINFO*>(&bih), DIB_RGB_COLORS)) {
        ReleaseDC(NULL, hdc);
        return false;
    }
    ReleaseDC(NULL, hdc);

    BITMAPFILEHEADER bfh = {0};
    bfh.bfType = 0x4D42; // "BM"
    bfh.bfSize = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + dwSize;
    bfh.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);

    HANDLE hFile = CreateFileW(filePath.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return false;

    DWORD bytesWritten = 0;
    WriteFile(hFile, &bfh, sizeof(bfh), &bytesWritten, NULL);
    WriteFile(hFile, &bih, sizeof(bih), &bytesWritten, NULL);
    WriteFile(hFile, buf.data(), dwSize, &bytesWritten, NULL);
    CloseHandle(hFile);

    return true;
}

int TestDecodeFile(const std::wstring& filePath, UINT maxEdge, const std::wstring& outBmpPath) {
    std::wcout << L"Testing native direct decoder for: " << filePath << L"\n";
    if (!fs::exists(filePath)) {
        std::wcout << L"ERROR: File does not exist: " << filePath << L"\n";
        return 1;
    }

    IStream* pStream = nullptr;
    HRESULT hr = SHCreateStreamOnFileW(filePath.c_str(), STGM_READ | STGM_SHARE_DENY_NONE, &pStream);
    if (FAILED(hr)) {
        std::wcout << L"ERROR: Failed to open file stream (0x" << std::hex << hr << std::dec << L")\n";
        return 2;
    }

    auto start = std::chrono::high_resolution_clock::now();
    HBITMAP hbmp = NULL;
    WTS_ALPHATYPE alpha = WTSAT_UNKNOWN;
    PsdThumbnail::DecodeResult result;

    hr = PsdThumbnail::PsdDecoder::Decode(pStream, maxEdge, &hbmp, &alpha, &result);
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> elapsed = end - start;

    pStream->Release();

    if (SUCCEEDED(hr) && hbmp != NULL) {
        std::wcout << L"SUCCESS: Decoded thumbnail in " << std::fixed << std::setprecision(2)
                   << elapsed.count() << L" ms!\n";
        std::wcout << L"Source: ";
        switch (result.source) {
            case PsdThumbnail::DecodeSource::EmbeddedThumbnail: std::wcout << L"Embedded JPEG (Fast Path)\n"; break;
            case PsdThumbnail::DecodeSource::XmpThumbnail: std::wcout << L"XMP Metadata (Fast Path)\n"; break;
            case PsdThumbnail::DecodeSource::CompositeImageData: std::wcout << L"Composite Image Data (Secondary Path)\n"; break;
            default: std::wcout << L"Unknown\n"; break;
        }
        std::wcout << L"Output Dimensions: " << result.outputWidth << L"x" << result.outputHeight << L"\n";
        std::wcout << L"Alpha: " << (result.hasAlpha ? L"ARGB (Alpha Channel Present)" : L"RGB (Opaque)") << L"\n";

        if (!outBmpPath.empty()) {
            if (SaveHBitmapToBmp(hbmp, outBmpPath)) {
                std::wcout << L"Saved thumbnail preview to: " << outBmpPath << L"\n";
            }
        }
        DeleteObject(hbmp);
        return 0;
    } else {
        std::wcout << L"FAILED: Decode failed with HRESULT 0x" << std::hex << hr << std::dec << L"\n";
        return 3;
    }
}

int TestComProvider(const std::wstring& filePath, UINT maxEdge, const std::wstring& outBmpPath) {
    std::wcout << L"Testing COM IThumbnailProvider instantiation for: " << filePath << L"\n";
    CLSID clsid;
    if (FAILED(CLSIDFromString(CLSID_OUR_STR, &clsid))) {
        std::wcout << L"ERROR: Failed to parse CLSID: " << CLSID_OUR_STR << L"\n";
        return 1;
    }

    IUnknown* pUnk = nullptr;
    HRESULT hr = CoCreateInstance(clsid, NULL, CLSCTX_INPROC_SERVER, IID_IUnknown, reinterpret_cast<void**>(&pUnk));
    if (FAILED(hr)) {
        std::wcout << L"ERROR: CoCreateInstance failed: 0x" << std::hex << hr << std::dec
                   << L" (Is the DLL registered?)\n";
        return 2;
    }

    IInitializeWithStream* pInit = nullptr;
    hr = pUnk->QueryInterface(IID_IInitializeWithStream, reinterpret_cast<void**>(&pInit));
    if (FAILED(hr)) {
        pUnk->Release();
        std::wcout << L"ERROR: Failed to QueryInterface for IInitializeWithStream: 0x" << std::hex << hr << std::dec << L"\n";
        return 3;
    }

    IStream* pStream = nullptr;
    hr = SHCreateStreamOnFileW(filePath.c_str(), STGM_READ | STGM_SHARE_DENY_NONE, &pStream);
    if (FAILED(hr)) {
        pInit->Release();
        pUnk->Release();
        std::wcout << L"ERROR: Failed to open file stream: " << filePath << L"\n";
        return 4;
    }

    hr = pInit->Initialize(pStream, STGM_READ);
    pStream->Release();
    pInit->Release();
    if (FAILED(hr)) {
        pUnk->Release();
        std::wcout << L"ERROR: Initialize(pStream) failed: 0x" << std::hex << hr << std::dec << L"\n";
        return 5;
    }

    IThumbnailProvider* pThumb = nullptr;
    hr = pUnk->QueryInterface(__uuidof(IThumbnailProvider), reinterpret_cast<void**>(&pThumb));
    pUnk->Release();
    if (FAILED(hr)) {
        std::wcout << L"ERROR: Failed to QueryInterface for IThumbnailProvider: 0x" << std::hex << hr << std::dec << L"\n";
        return 6;
    }

    auto start = std::chrono::high_resolution_clock::now();
    HBITMAP hbmp = NULL;
    WTS_ALPHATYPE alpha = WTSAT_UNKNOWN;
    hr = pThumb->GetThumbnail(maxEdge, &hbmp, &alpha);
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> elapsed = end - start;
    pThumb->Release();

    if (SUCCEEDED(hr) && hbmp != NULL) {
        std::wcout << L"SUCCESS: COM GetThumbnail completed in " << std::fixed << std::setprecision(2)
                   << elapsed.count() << L" ms!\n";
        if (!outBmpPath.empty()) {
            if (SaveHBitmapToBmp(hbmp, outBmpPath)) {
                std::wcout << L"Saved COM preview to: " << outBmpPath << L"\n";
            }
        }
        DeleteObject(hbmp);
        return 0;
    } else {
        std::wcout << L"FAILED: GetThumbnail returned 0x" << std::hex << hr << std::dec << L"\n";
        return 7;
    }
}

int TestShellItemImageFactory(const std::wstring& filePath, UINT maxEdge, const std::wstring& outBmpPath) {
    std::wcout << L"Testing Windows Explorer Shell (IShellItemImageFactory) for: " << filePath << L"\n";
    IShellItem* pItem = nullptr;
    HRESULT hr = SHCreateItemFromParsingName(filePath.c_str(), NULL, IID_PPV_ARGS(&pItem));
    if (FAILED(hr)) {
        std::wcout << L"ERROR: SHCreateItemFromParsingName failed: 0x" << std::hex << hr << std::dec << L"\n";
        return 1;
    }

    IShellItemImageFactory* pFactory = nullptr;
    hr = pItem->QueryInterface(IID_PPV_ARGS(&pFactory));
    pItem->Release();
    if (FAILED(hr)) {
        std::wcout << L"ERROR: QueryInterface IShellItemImageFactory failed: 0x" << std::hex << hr << std::dec << L"\n";
        return 2;
    }

    SIZE sz = { static_cast<LONG>(maxEdge), static_cast<LONG>(maxEdge) };
    HBITMAP hbmp = NULL;
    auto start = std::chrono::high_resolution_clock::now();
    hr = pFactory->GetImage(sz, SIIGBF_THUMBNAILONLY, &hbmp);
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> elapsed = end - start;
    pFactory->Release();

    if (SUCCEEDED(hr) && hbmp != NULL) {
        std::wcout << L"Windows Explorer IShellItemImageFactory SUCCESS in " << std::fixed << std::setprecision(2)
                   << elapsed.count() << L" ms!\n";
        if (!outBmpPath.empty()) {
            if (SaveHBitmapToBmp(hbmp, outBmpPath)) {
                std::wcout << L"Saved Explorer shell preview to: " << outBmpPath << L"\n";
            }
        }
        DeleteObject(hbmp);
        return 0;
    } else {
        std::wcout << L"Windows Explorer Shell failed: 0x" << std::hex << hr << std::dec << L"\n";
        return 3;
    }
}

typedef HRESULT (__stdcall *pfnDllReg)(void);

int CallDllEntry(const std::wstring& dllPath, bool bRegister) {
    HMODULE hMod = LoadLibraryW(dllPath.c_str());
    if (!hMod) {
        std::wcout << L"ERROR: Failed to load DLL: " << dllPath
                   << L" (Error: " << GetLastError() << L")\n";
        return 1;
    }

    const char* fnName = bRegister ? "DllRegisterServer" : "DllUnregisterServer";
    pfnDllReg pFn = reinterpret_cast<pfnDllReg>(GetProcAddress(hMod, fnName));
    if (!pFn) {
        std::wcout << L"ERROR: DLL does not export " << fnName << L"\n";
        FreeLibrary(hMod);
        return 2;
    }

    HRESULT hr = pFn();
    FreeLibrary(hMod);

    if (SUCCEEDED(hr)) {
        std::wcout << (bRegister ? L"Registered successfully!\n" : L"Unregistered successfully!\n");
        return 0;
    } else {
        std::wcout << (bRegister ? L"Registration failed: 0x" : L"Unregistration failed: 0x")
                   << std::hex << hr << std::dec << L"\n";
        return 3;
    }
}

int wmain(int argc, wchar_t* argv[]) {
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    if (argc < 2) {
        InspectRegistry();
        std::wcout << L"\nUsage:\n";
        std::wcout << L"  psdthumb-diag /status\n";
        std::wcout << L"  psdthumb-diag /register [dll_path]\n";
        std::wcout << L"  psdthumb-diag /unregister [dll_path]\n";
        std::wcout << L"  psdthumb-diag /repair [dll_path]\n";
        std::wcout << L"  psdthumb-diag /notify\n";
        std::wcout << L"  psdthumb-diag /test-com <file.psd/ai> [maxEdge] [out.bmp]\n";
        std::wcout << L"  psdthumb-diag /test-shell <file.psd/ai> [maxEdge] [out.bmp]\n";
        std::wcout << L"  psdthumb-diag /decode <file.psd> [maxEdge] [out.bmp]\n";
        CoUninitialize();
        return 0;
    }

    std::wstring cmd = argv[1];
    if (cmd == L"/status") {
        InspectRegistry();
    } else if (cmd == L"/register") {
        std::wstring dllPath;
        if (argc >= 3) {
            dllPath = argv[2];
        } else {
            wchar_t exePath[MAX_PATH];
            GetModuleFileNameW(NULL, exePath, MAX_PATH);
            fs::path p(exePath);
            dllPath = (p.parent_path() / L"PsdThumbnailProvider.dll").wstring();
        }
        std::wcout << L"Registering DLL: " << dllPath << L"...\n";
        int res = CallDllEntry(dllPath, true);
        if (res == 0) {
            SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, NULL, NULL);
            std::wcout << L"Shell change notification broadcast sent.\n";
        }
        CoUninitialize();
        return res;
    } else if (cmd == L"/unregister") {
        std::wstring dllPath;
        if (argc >= 3) {
            dllPath = argv[2];
        } else {
            wchar_t exePath[MAX_PATH];
            GetModuleFileNameW(NULL, exePath, MAX_PATH);
            fs::path p(exePath);
            dllPath = (p.parent_path() / L"PsdThumbnailProvider.dll").wstring();
        }
        std::wcout << L"Unregistering DLL: " << dllPath << L"...\n";
        int res = CallDllEntry(dllPath, false);
        if (res == 0) {
            SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, NULL, NULL);
            std::wcout << L"Shell change notification broadcast sent.\n";
        }
        CoUninitialize();
        return res;
    } else if (cmd == L"/repair") {
        std::wstring dllPath;
        if (argc >= 3) {
            dllPath = argv[2];
        } else {
            wchar_t exePath[MAX_PATH];
            GetModuleFileNameW(NULL, exePath, MAX_PATH);
            fs::path p(exePath);
            dllPath = (p.parent_path() / L"PsdThumbnailProvider.dll").wstring();
        }
        std::wcout << L"Repairing registration for: " << dllPath << L"...\n";
        CallDllEntry(dllPath, false);
        int res = CallDllEntry(dllPath, true);
        SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, NULL, NULL);
        std::wcout << L"Repair completed with result code " << res << L".\n";
        CoUninitialize();
        return res;
    } else if (cmd == L"/notify") {
        SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, NULL, NULL);
        std::wcout << L"Shell association change notification (SHCNE_ASSOCCHANGED) sent.\n";
    } else if (cmd == L"/decode") {
        if (argc < 3) {
            std::wcout << L"Usage: psdthumb-diag /decode <file.psd> [maxEdge] [out.bmp]\n";
            CoUninitialize();
            return 1;
        }
        std::wstring file = argv[2];
        UINT maxEdge = 256;
        if (argc >= 4) {
            maxEdge = static_cast<UINT>(_wtoi(argv[3]));
            if (maxEdge == 0) maxEdge = 256;
        }
        std::wstring outBmp;
        if (argc >= 5) {
            outBmp = argv[4];
        }
        int res = TestDecodeFile(file, maxEdge, outBmp);
        CoUninitialize();
        return res;
    } else if (cmd == L"/test-com") {
        if (argc < 3) {
            std::wcout << L"Usage: psdthumb-diag /test-com <file.psd/ai> [maxEdge] [out.bmp]\n";
            CoUninitialize();
            return 1;
        }
        std::wstring file = argv[2];
        UINT maxEdge = 256;
        if (argc >= 4) {
            maxEdge = static_cast<UINT>(_wtoi(argv[3]));
            if (maxEdge == 0) maxEdge = 256;
        }
        std::wstring outBmp;
        if (argc >= 5) {
            outBmp = argv[4];
        }
        int res = TestComProvider(file, maxEdge, outBmp);
        CoUninitialize();
        return res;
    } else if (cmd == L"/test-shell") {
        if (argc < 3) {
            std::wcout << L"Usage: psdthumb-diag /test-shell <file.psd/ai> [maxEdge] [out.bmp]\n";
            CoUninitialize();
            return 1;
        }
        std::wstring file = argv[2];
        UINT maxEdge = 256;
        if (argc >= 4) {
            maxEdge = static_cast<UINT>(_wtoi(argv[3]));
            if (maxEdge == 0) maxEdge = 256;
        }
        std::wstring outBmp;
        if (argc >= 5) {
            outBmp = argv[4];
        }
        int res = TestShellItemImageFactory(file, maxEdge, outBmp);
        CoUninitialize();
        return res;
    } else {
        std::wcout << L"Unknown option: " << cmd << L"\n";
    }

    CoUninitialize();
    return 0;
}
