# PSD & AI Native Windows Shell Thumbnail Provider

A high-performance, native 64-bit Windows Shell COM Extension (`IThumbnailProvider` & `IInitializeWithStream`) providing instant thumbnail previews in Windows 11 and Windows 10 File Explorer for:
- **Adobe Photoshop PSD** (`.psd`)
- **Adobe Photoshop Large Document Format PSB** (`.psb`)
- **Adobe Illustrator** (`.ai`)

---

## Features & Highlights

- **Native 64-bit In-Process COM Server**: Implements `IThumbnailProvider` and `IInitializeWithStream`. Runs directly inside the Windows Explorer surrogate host (`dllhost.exe`) with native execution speed.
- **Pure Modern C++20**: Statically links the C++ runtime and utilizes the Universal C Runtime (UCRT) native to Windows 10 and 11. Requires zero external codecs, .NET runtimes, or third-party background services.
- **Size-Aware Dual-Path Streaming Architecture**:
  - **Fast Path (Embedded JPEG & XMP):** Instantly extracts embedded preview images (8BIM tags 1036/1033) and base64 XMP metadata (tag 1060 / Illustrator XMP) decoded via Windows Imaging Component (WIC) in ~4–12 ms.
  - **High-Resolution Composite Decompression:** When larger thumbnail resolutions are requested (Preview Pane, Extra Large Icons) or when embedded previews are absent, decodes PackBits RLE and Raw planar channels directly.
  - **Intelligent Row-Skipping Seek:** Uses scanline prefix offsets to read only the rows required for thumbnail downsampling, avoiding full-file downloads on cloud-synced folders (e.g. OneDrive Files On-Demand).
  - **High-Quality Scaling:** Scales down using WIC Fant bicubic interpolation to fit the exact requested thumbnail dimensions (`cx`), preserving crisp text and lines without nearest-neighbor pixelation.
- **Accurate Color & Alpha Blending**:
  - Full CMYK inverted-ink channel handling with accurate conversion to RGB.
  - Alpha transparency premultiplication for clean, fringe-free rendering across light and dark Windows Explorer themes.
  - Unsupported color modes (e.g. Bitmap, Indexed, Lab) gracefully fail cleanly so Explorer displays the default generic icon instead of caching blank or black thumbnails.
- **Non-Destructive Ownership-Aware Registration**:
  - Uses a dedicated independent GUID: `{66FC65C5-2883-4D88-82AF-70626AF90EE8}`.
  - Binds strictly via `ShellEx\{e357fccd-a995-4576-b01f-234630154e96}` and `SystemFileAssociations`.
  - Automatically backs up any pre-existing third-party handlers prior to installation, and restores them if uninstalled.
  - Preserves application ProgIDs and default associations ("Open With" settings remain untouched).
- **Instant Explorer Notification**: Broadcasts `SHChangeNotify(SHCNE_ASSOCCHANGED)` on registration and unregistration to refresh File Explorer icons without requiring a system reboot.
- **Diagnostic CLI**: Includes `psdthumb-diag.exe` to inspect registry health, test COM instantiation, diagnose ProgID precedence, and verify end-to-end Shell performance.

---

## Verified Benchmarks

Tested on real production assets using 256×256 thumbnail generation:

| Asset Type | File Size | Decode Pipeline | Latency |
|---|---|---|---|
| **High-Resolution Photoshop PSD** | 88 MB | Composite Image Data | **~34.2 ms** |
| **Photoshop Artwork PSD** | 32 MB | Embedded Fast-Path | **~4.1 ms** |
| **Vector Artwork `.ai`** | 3.2 MB | XMP Embedded Preview | **~4.1 ms** |
| **Windows Explorer Shell API (`IShellItemImageFactory`)** | — | End-to-End Windows Pipeline | **~10 – 20 ms** |

---

## Installation & Quick Start

### Option 1: Pre-Compiled Release (Recommended)
1. Download the latest `psd-thumbnail-provider-vX.X.X.zip` from [Releases](https://github.com/nurfarhad/psd-thumbnail-provider/releases).
2. Extract the archive to a permanent folder on your system (e.g. `C:\Program Files\PSD Thumbnail Provider` or `G:\Code\PSD_Thumbnail_Provider`).
3. Right-click `install.bat` and run it. The installer will register the 64-bit extension and notify File Explorer.

To uninstall cleanly at any time, run `uninstall.bat`.

### Option 2: Build from Source
To compile `PsdThumbnailProvider.dll` and `psdthumb-diag.exe` on Windows:

1. Install LLVM-MinGW or Clang (e.g. via `winget install MartinStorsjo.LLVM-MinGW.UCRT`).
2. Run `build.bat`:
   ```bat
   build.bat
   ```
   Both 64-bit binaries will be compiled with Unicode support and version resources into the `bin/` directory.

---

## Diagnostic CLI Commands

The included `psdthumb-diag.exe` tool provides full registry introspection and decoding verification:

```bat
# Check live registry, architecture, & ProgID status:
bin\psdthumb-diag.exe /status

# Test Windows Explorer Shell engine (IShellItemImageFactory):
bin\psdthumb-diag.exe /test-shell "path\to\file.psd" 256 "output.bmp"

# Test Windows COM instantiation on a file:
bin\psdthumb-diag.exe /test-com "path\to\file.psd" 256 "output.bmp"

# Test direct native stream decoding:
bin\psdthumb-diag.exe /decode "path\to\file.psd" 256 "output.bmp"

# Repair or re-register bindings:
bin\psdthumb-diag.exe /repair "bin\PsdThumbnailProvider.dll"

# Unregister the DLL cleanly:
bin\psdthumb-diag.exe /unregister "bin\PsdThumbnailProvider.dll"
```

---

## Supported Specifications & Formats

- **Photoshop PSD**: Version 1 (up to 30,000 × 30,000 px).
- **Photoshop PSB**: Version 2 Large Document Format (up to 300,000 × 300,000 px) with 64-bit stream offsets and 4-byte RLE counts.
- **Adobe Illustrator AI**: Embedded XMP thumbnails with head and tail scan.
- **Color Modes**: RGB (8-bit), Grayscale (8-bit), and CMYK (8-bit inverted ink). Embedded previews decoded for all modes where present.

---

## Disclaimer

Adobe, Photoshop, and Illustrator are registered trademarks of Adobe Inc. This project is an independent open-source utility and is not affiliated with, endorsed by, or sponsored by Adobe Inc.

---

## License

MIT License. See [LICENSE](LICENSE) for details.
