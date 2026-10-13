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
#include "../core/psd_decoder.h"

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "gdi32.lib")

namespace fs = std::filesystem;

static const wchar_t* CLSID_OUR_STR = L"{66FC65C5-2883-4D88-82AF-70626AF90EE8}";
static const wchar_t* IID_THUMB_STR  = L"{e357fccd-a995-4576-b01f-234630154e96}";
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
bool ReadRegString(HKEY root, const std::wstring& subKey, const std::wstring& valueName, std::wstring& outValue) {
    HKEY hKey = NULL;
    LONG res = RegOpenKeyExW(root, subKey.c_str(), 0, KEY_READ, &hKey);
    if (res != ERROR_SUCCESS) return false;

    wchar_t buf[1024];
    DWORD cbData = sizeof(buf);
    DWORD dwType = 0;
    const wchar_t* valPtr = valueName.empty() ? nullptr : valueName.c_str();
    res = RegQueryValueExW(hKey, valPtr, NULL, &dwType, reinterpret_cast<BYTE*>(buf), &cbData);
    RegCloseKey(hKey);

    if (res == ERROR_SUCCESS && (dwType == REG_SZ || dwType == REG_EXPAND_SZ)) {
        outValue = buf;
        return true;
    }
    return false;
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
    } else {
        std::wcout << L"  [HKLM] Not registered in HKLM\n";
    }

    if (hasHkcuClsid) {
        std::wcout << L"  [HKCU] Registered: " << hkcuDll << L"\n";
        ReadRegString(HKEY_CURRENT_USER, inprocKey, L"ThreadingModel", threading);
        std::wcout << L"         ThreadingModel: " << threading << L"\n";
        bool exists = fs::exists(hkcuDll);
        std::wcout << L"         File exists: " << (exists ? L"YES" : L"NO (MISSING FILE!)") << L"\n";
    } else {
        std::wcout << L"  [HKCU] Not registered in HKCU\n";
    }

    // 2. Check File Association ShellEx
    std::wcout << L"\n--- [File Associations ShellEx] ---\n";
    const std::vector<std::wstring> exts = { L".psd", L".psb", L".ai" };
    for (const auto& ext : exts) {
        std::wcout << L"Extension " << ext << L":\n";

        // Check HKCU (which overrides HKLM)
        std::wstring hkcuKey = L"Software\\Classes\\" + ext + L"\\ShellEx\\" + IID_THUMB_STR;
        std::wstring hkcuVal;
        if (ReadRegString(HKEY_CURRENT_USER, hkcuKey, L"", hkcuVal)) {
            std::wcout << L"  * HKCU Classes\\" << ext << L"\\ShellEx: " << hkcuVal;
            if (_wcsicmp(hkcuVal.c_str(), CLSID_OUR_STR) == 0) {
                std::wcout << L" (Our Provider)\n";
            } else if (_wcsicmp(hkcuVal.c_str(), ARDFRY_CLSID) == 0) {
                std::wcout << L" (Ardfry PSD Codec - competing/orphaned!)\n";
            } else {
                std::wcout << L" (Other handler)\n";
            }
        } else {
            std::wcout << L"  * HKCU Classes\\" << ext << L"\\ShellEx: (not set)\n";
        }

        // Check HKLM
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

// Save HBITMAP to 24/32-bit BMP file for test verification
bool SaveBitmapToBmp(HBITMAP hBitmap, const std::wstring& outPath) {
    if (!hBitmap) return false;
    BITMAP bm;
    if (!GetObject(hBitmap, sizeof(bm), &bm)) return false;

    BITMAPINFOHEADER bi = {};
    bi.biSize = sizeof(BITMAPINFOHEADER);
    bi.biWidth = bm.bmWidth;
    bi.biHeight = bm.bmHeight;
    bi.biPlanes = 1;
    bi.biBitCount = 32;
    bi.biCompression = BI_RGB;

    DWORD dwSizeImage = bm.bmWidth * bm.bmHeight * 4;
    std::vector<BYTE> pixels(dwSizeImage);

    HDC hdc = GetDC(NULL);
    if (!GetDIBits(hdc, hBitmap, 0, bm.bmHeight, pixels.data(), (BITMAPINFO*)&bi, DIB_RGB_COLORS)) {
        ReleaseDC(NULL, hdc);
        return false;
    }
    ReleaseDC(NULL, hdc);

    BITMAPFILEHEADER bfh = {};
    bfh.bfType = 0x4D42; // "BM"
    bfh.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    bfh.bfSize = bfh.bfOffBits + dwSizeImage;

    HANDLE hFile = CreateFileW(outPath.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return false;

    DWORD written = 0;
    WriteFile(hFile, &bfh, sizeof(bfh), &written, NULL);
    WriteFile(hFile, &bi, sizeof(bi), &written, NULL);
    WriteFile(hFile, pixels.data(), dwSizeImage, &written, NULL);
    CloseHandle(hFile);

    return true;
}

// Decode test
int TestDecodeFile(const std::wstring& filePath, UINT maxEdge, const std::wstring& saveBmpPath) {
    std::wcout << L"Testing decode on: " << filePath << L"\n";
    if (!fs::exists(filePath)) {
        std::wcout << L"ERROR: File does not exist!\n";
        return 1;
    }

    uintmax_t fileSize = fs::file_size(filePath);
    std::wcout << L"File size: " << fileSize << L" bytes (" << (fileSize / 1024 / 1024) << L" MB)\n";

    IStream* pStream = nullptr;
    HRESULT hr = SHCreateStreamOnFileEx(filePath.c_str(), STGM_READ | STGM_SHARE_DENY_NONE, FILE_ATTRIBUTE_NORMAL, FALSE, NULL, &pStream);
    if (FAILED(hr) || !pStream) {
        std::wcout << L"ERROR: Failed to open IStream: 0x" << std::hex << hr << std::dec << L"\n";
        return 2;
    }

    HBITMAP hBitmap = NULL;
    WTS_ALPHATYPE alphaType = WTSAT_UNKNOWN;
    PsdThumbnail::DecodeResult result;

    auto start = std::chrono::high_resolution_clock::now();
    hr = PsdThumbnail::PsdDecoder::Decode(pStream, maxEdge, &hBitmap, &alphaType, &result);
    auto end = std::chrono::high_resolution_clock::now();
    double elapsedMs = std::chrono::duration<double, std::milli>(end - start).count();

    pStream->Release();

    if (FAILED(hr) || !hBitmap) {
        std::wcout << L"ERROR: Decode failed with HRESULT: 0x" << std::hex << hr << std::dec << L"\n";
        return 3;
    }

    std::wcout << L"Decode SUCCESS in " << std::fixed << std::setprecision(2) << elapsedMs << L" ms!\n";
    const wchar_t* srcStr = L"Unknown";
    switch (result.source) {
        case PsdThumbnail::DecodeSource::EmbeddedThumbnail: srcStr = L"Embedded Thumbnail (Resource 1036/1033 JPEG)"; break;
        case PsdThumbnail::DecodeSource::XmpThumbnail: srcStr = L"XMP Thumbnail (Resource 1060 base64 JPEG)"; break;
        case PsdThumbnail::DecodeSource::CompositeImageData: srcStr = L"Composite Image Data (RLE / Raw)"; break;
        default: break;
    }
    std::wcout << L"Source: " << srcStr << L"\n";
    std::wcout << L"Original Dimensions: " << result.originalWidth << L" x " << result.originalHeight << L"\n";
    std::wcout << L"Output Dimensions:   " << result.outputWidth << L" x " << result.outputHeight << L"\n";
    std::wcout << L"Alpha Type:          " << (alphaType == WTSAT_ARGB ? L"ARGB (Alpha present)" : L"RGB (Solid)") << L"\n";

    if (!saveBmpPath.empty()) {
        if (SaveBitmapToBmp(hBitmap, saveBmpPath)) {
            std::wcout << L"Saved thumbnail preview to: " << saveBmpPath << L"\n";
        } else {
            std::wcout << L"Failed to save preview BMP\n";
        }
    }

    DeleteObject(hBitmap);
    return 0;
}

// Test via Windows COM Subsystem (CoCreateInstance)
int TestComProvider(const std::wstring& filePath, UINT maxEdge, const std::wstring& saveBmpPath) {
    std::wcout << L"Testing COM Provider instantiation for: " << filePath << L"\n";
    if (!fs::exists(filePath)) {
        std::wcout << L"ERROR: File does not exist!\n";
        return 1;
    }

    CLSID clsid;
    HRESULT hr = CLSIDFromString(CLSID_OUR_STR, &clsid);
    if (FAILED(hr)) {
        std::wcout << L"ERROR: Failed to parse CLSID: " << CLSID_OUR_STR << L"\n";
        return 2;
    }

    IInitializeWithStream* pInit = nullptr;
    hr = CoCreateInstance(clsid, NULL, CLSCTX_INPROC_SERVER, IID_IInitializeWithStream, (void**)&pInit);
    if (FAILED(hr) || !pInit) {
        std::wcout << L"ERROR: CoCreateInstance failed with HRESULT: 0x" << std::hex << hr << std::dec << L"\n";
        return 3;
    }
    std::wcout << L"COM Object created successfully (CoCreateInstance OK)!\n";

    IStream* pStream = nullptr;
    hr = SHCreateStreamOnFileEx(filePath.c_str(), STGM_READ | STGM_SHARE_DENY_NONE, FILE_ATTRIBUTE_NORMAL, FALSE, NULL, &pStream);
    if (FAILED(hr) || !pStream) {
        std::wcout << L"ERROR: Failed to open IStream: 0x" << std::hex << hr << std::dec << L"\n";
        pInit->Release();
        return 4;
    }

    hr = pInit->Initialize(pStream, STGM_READ);
    if (FAILED(hr)) {
        std::wcout << L"ERROR: IInitializeWithStream::Initialize failed: 0x" << std::hex << hr << std::dec << L"\n";
        pStream->Release();
        pInit->Release();
        return 5;
    }

    IThumbnailProvider* pThumb = nullptr;
    hr = pInit->QueryInterface(__uuidof(IThumbnailProvider), (void**)&pThumb);
    pInit->Release();
    if (FAILED(hr) || !pThumb) {
        std::wcout << L"ERROR: QueryInterface(IThumbnailProvider) failed: 0x" << std::hex << hr << std::dec << L"\n";
        pStream->Release();
        return 6;
    }

    HBITMAP hBitmap = NULL;
    WTS_ALPHATYPE alphaType = WTSAT_UNKNOWN;

    auto start = std::chrono::high_resolution_clock::now();
    hr = pThumb->GetThumbnail(maxEdge, &hBitmap, &alphaType);
    auto end = std::chrono::high_resolution_clock::now();
    double elapsedMs = std::chrono::duration<double, std::milli>(end - start).count();

    pThumb->Release();
    pStream->Release();

    if (FAILED(hr) || !hBitmap) {
        std::wcout << L"ERROR: IThumbnailProvider::GetThumbnail failed: 0x" << std::hex << hr << std::dec << L"\n";
        return 7;
    }

    std::wcout << L"IThumbnailProvider::GetThumbnail SUCCESS in " << std::fixed << std::setprecision(2) << elapsedMs << L" ms!\n";
    std::wcout << L"Alpha Type: " << (alphaType == WTSAT_ARGB ? L"ARGB" : L"RGB") << L"\n";

    if (!saveBmpPath.empty()) {
        SaveBitmapToBmp(hBitmap, saveBmpPath);
        std::wcout << L"Saved preview to: " << saveBmpPath << L"\n";
    }

    DeleteObject(hBitmap);
    return 0;
}

// Test via Windows Shell IShellItemImageFactory (The exact Explorer API)
int TestShellItemImageFactory(const std::wstring& filePath, UINT maxEdge, const std::wstring& saveBmpPath) {
    std::wcout << L"Testing Windows Explorer Shell (IShellItemImageFactory) for: " << filePath << L"\n";
    if (!fs::exists(filePath)) {
        std::wcout << L"ERROR: File does not exist!\n";
        return 1;
    }

    IShellItemImageFactory* pFactory = nullptr;
    HRESULT hr = SHCreateItemFromParsingName(filePath.c_str(), NULL, IID_IShellItemImageFactory, (void**)&pFactory);
    if (FAILED(hr) || !pFactory) {
        std::wcout << L"ERROR: SHCreateItemFromParsingName failed: 0x" << std::hex << hr << std::dec << L"\n";
        return 2;
    }

    SIZE sz = { (LONG)maxEdge, (LONG)maxEdge };
    HBITMAP hBitmap = NULL;
    auto start = std::chrono::high_resolution_clock::now();
    hr = pFactory->GetImage(sz, SIIGBF_THUMBNAILONLY, &hBitmap);
    auto end = std::chrono::high_resolution_clock::now();
    double elapsedMs = std::chrono::duration<double, std::milli>(end - start).count();

    pFactory->Release();

    if (FAILED(hr) || !hBitmap) {
        std::wcout << L"IShellItemImageFactory::GetImage failed: 0x" << std::hex << hr << std::dec << L"\n";
        return 3;
    }

    std::wcout << L"Windows Explorer IShellItemImageFactory SUCCESS in " << std::fixed << std::setprecision(2) << elapsedMs << L" ms!\n";
    if (!saveBmpPath.empty()) {
        SaveBitmapToBmp(hBitmap, saveBmpPath);
        std::wcout << L"Saved Explorer shell preview to: " << saveBmpPath << L"\n";
    }

    DeleteObject(hBitmap);
    return 0;
}

// Register or unregister DLL directly
int CallDllEntry(const std::wstring& dllPath, bool bRegister) {
    HMODULE hMod = LoadLibraryW(dllPath.c_str());
    if (!hMod) {
        std::wcout << L"Failed to load DLL: " << dllPath << L" (Error: " << GetLastError() << L")\n";
        return 1;
    }

    typedef HRESULT (__stdcall *FnReg)();
    const char* fnName = bRegister ? "DllRegisterServer" : "DllUnregisterServer";
    FnReg pFn = (FnReg)GetProcAddress(hMod, fnName);
    if (!pFn) {
        std::wcout << L"Failed to find entry point " << fnName << L"\n";
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
        std::wcout << L"  psdthumb-diag /notify\n";
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
            maxEdge = (UINT)_wtoi(argv[3]);
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
            maxEdge = (UINT)_wtoi(argv[3]);
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
            maxEdge = (UINT)_wtoi(argv[3]);
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
