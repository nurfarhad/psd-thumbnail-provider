# PSD & AI Native Windows Shell Thumbnail Provider

A high-performance, native 64-bit Windows Shell COM Extension (`IThumbnailProvider` & `IInitializeWithStream`) providing instant thumbnail previews in Windows 11 and Windows 10 File Explorer for:
- **Adobe Photoshop PSD** (`.psd`)
- **Adobe Photoshop Large Document Format PSB** (`.psb`)
- **Adobe Illustrator** (`.ai`)

---

## Features & Highlights

- **Native 64-bit In-Process COM DLL**: Implements `IThumbnailProvider` and `IInitializeWithStream`. Runs directly inside the Windows Shell surrogate process with zero overhead.
- **Pure C++20 with Zero Dependencies**: Statically linked against the Universal C Runtime (UCRT). Requires no third-party runtimes, .NET installations, or external codecs.
- **Dual-Path Streaming Decoder**:
  - **Fast Path (Embedded JPEG & XMP):** Extracts embedded previews from 8BIM resources (tags 1036/1033) and base64 XMP image metadata (tag 1060 / Illustrator XMP) decoded via Windows Imaging Component (WIC) in ~4 milliseconds.
  - **Secondary Path (Image Data Composite):** Decompresses PackBits RLE (v1 2-byte counts and v2 4-byte counts) and raw uncompressed channels with streaming row subsampling, supporting multi-gigabyte files with low memory usage.
- **Non-Destructive Registration**:
  - Utilizes dedicated independent GUID: `{66FC65C5-2883-4D88-82AF-70626AF90EE8}`.
  - Binds strictly via `ShellEx\{e357fccd-a995-4576-b01f-234630154e96}` and `SystemFileAssociations`.
  - Never hijacks or alters default file associations or application ProgIDs ("Open With" settings remain untouched).
- **Instant Live Notification**: Calls `SHChangeNotify(SHCNE_ASSOCCHANGED)` upon registration and unregistration to instantly refresh File Explorer without needing a system reboot.
- **Diagnostic & Verification CLI**: Includes `psdthumb-diag.exe` to inspect registry health, test COM decoding, and benchmark Windows Shell engine latency.

---

## Verified Benchmarks

Tested on real production assets using high-resolution 256×256 thumbnail generation:

| Asset Type | File Size | Decode Method | Latency |
|---|---|---|---|
| **High-Resolution Photoshop PSD** | 88 MB | Fast-Path Embedded JPEG | **~3.96 ms** |
| **Multi-Layer Document PSD** | 12 MB | Fast-Path Embedded JPEG | **~4.48 ms** |
| **Complex Canvas Artwork PSD** | 32 MB | Fast-Path Embedded JPEG | **~4.07 ms** |
| **Vector Artwork `.ai`** | 3.2 MB | XMP Embedded JPEG | **~5.09 ms** |
| **Complex Vector Graphic `.ai`** | 2.2 MB | XMP Embedded JPEG | **~18.25 ms** |
| **Windows Explorer Shell API (`IShellItemImageFactory`)** | — | End-to-End Shell Pipeline | **~18 - 21 ms** |

---

## Quick Start & Installation

### Option 1: 1-Click Install
Run `install.bat` as administrator or standard user. It registers the DLL and broadcasts the change notification to File Explorer.

```bat
install.bat
```

To remove at any time:
```bat
uninstall.bat
```

---

## Building from Source

To compile `PsdThumbnailProvider.dll` and `psdthumb-diag.exe` directly on Windows:

1. Ensure LLVM-MinGW or Clang is installed (e.g. via `winget install MartinStorsjo.LLVM-MinGW.UCRT`).
2. Run `build.bat`:
   ```bat
   build.bat
   ```
   Both 64-bit binaries will be compiled and placed into the `bin/` directory.

---

## Diagnostic CLI Commands

The included `psdthumb-diag.exe` tool provides complete registry introspection and decoding verification:

```bat
# Check live registry & shell integration status:
bin\psdthumb-diag.exe /status

# Test Windows COM instantiation on a file:
bin\psdthumb-diag.exe /test-com "path\to\file.psd" 256 "output.bmp"

# Test Windows Explorer Shell engine (IShellItemImageFactory):
bin\psdthumb-diag.exe /test-shell "path\to\file.psd" 256 "output.bmp"

# Register the DLL manually:
bin\psdthumb-diag.exe /register "bin\PsdThumbnailProvider.dll"

# Unregister the DLL cleanly:
bin\psdthumb-diag.exe /unregister "bin\PsdThumbnailProvider.dll"
```

---

## License

MIT License. See [LICENSE](LICENSE) for details.
