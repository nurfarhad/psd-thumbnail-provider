# Audit Report: PSD Thumbnail Pro (Antigravity build)

**Audited:** `PSD_Thumbnail_Pro.exe`, `fix_psd_thumbnails.bat`
**Reference:** `PSDCodec-Setup.exe` (Ardfry PSD Codec v1.6.1.0 installer)
**Audit date:** 13 Oct 2026
**Method:** Static analysis only (nothing was executed, no Windows machine was used). .NET metadata and IL disassembly of the exe, string extraction, Inno Setup unpack, MSI table dump of the PSD Codec installer.

Evidence labels used below:
- **[Verified]** observed directly in the files.
- **[Inferred]** follows from documented Windows behaviour but must be confirmed on a real Windows machine.

| File | SHA-256 |
|---|---|
| PSD_Thumbnail_Pro.exe | `9c83dbf59b769f05c8ccb774038b2dffef58bee00aa41ab18e915138e7e09cf2` |
| fix_psd_thumbnails.bat | `d40f181b6ae0778502b965c1922131d68e00bc3f5af48cf993a96606410bed57` |
| PSDCodec-Setup.exe | `02508ca3b575ccd368b36ec0e6ae63c548e93aefe119692de09406ebd87d0ab0` |

---

## 1. Verdict

**The build cannot work as an Explorer thumbnail codec, by design.** There are three root causes:

1. **It is a desktop app, not a shell extension.** Windows Explorer only shows custom thumbnails when it can load a registered in-process COM DLL that implements `IThumbnailProvider`. `PSD_Thumbnail_Pro.exe` is a 24 KB WinForms gallery app. It has no exports, no COM server and no registration of its own.
2. **It points Windows at Ardfry's CLSIDs.** The `.bat` and the app's "Repair" button register Ardfry PSD Codec's class IDs. Those IDs only resolve if PSD Codec is installed. Without it, Explorer finds a handler with nothing behind it and falls back to the generic icon.
3. **The `.bat` never creates the binding that matters.** It never writes `.psd\ShellEx\{e357fccd-…}`, which is the key that enables thumbnails. It also overwrites the `.psd` file association with a ProgID that does not exist.

Even the fallback decoder is weak: it only extracts the JPEG thumbnail Photoshop may have embedded. It cannot decode the actual image.

**What to do:** stop patching the registry. Build a native COM DLL (the thing PSD Codec actually is), with its own GUID, its own decoder and a proper installer. Section 5 is the spec and section 8 is a ready-to-paste brief.

---

## 2. Findings

| ID | Severity | Finding |
|---|---|---|
| F-01 | Critical | The exe is not a shell extension |
| F-02 | Critical | No own COM server, depends on Ardfry's CLSIDs |
| F-03 | Critical | `.bat` does not create the thumbnail binding |
| F-04 | High | `.bat` overwrites the `.psd` default ProgID with a non-existent ProgID |
| F-05 | High | `.bat` has no elevation check, error handling, or undo |
| F-06 | High | No thumbnail cache or icon cache refresh |
| F-07 | High | Fallback is an embedded-thumbnail reader, not a decoder |
| F-08 | Medium | 32-bit app calling a COM class that may only exist as 64-bit |
| F-09 | Medium | "Repair" writes HKCU keys that shadow HKLM and outlive PSD Codec |
| F-10 | Medium | "Explorer Integration: Active" is a hard-coded label |
| F-11 | Medium | No `.ai`, no `.psb` |
| F-12 | Low | Hard-coded personal path, version 0.0.0.0, unsigned |
| F-13 | Low | Alpha channel is dropped in the viewer |
| F-14 | Info | Parts that are correct and reusable |
| F-15 | Legal | Reuse of Ardfry's identifiers and branding |

### F-01 (Critical): the exe is not a shell extension [Verified]
- PE header: x86 (`0x14C`), subsystem GUI, **no export directory**, not a DLL. .NET Framework 4.0 (`v4.0.30319`).
- `IThumbnailProvider` and `IInitializeWithStream` are declared as `[ComImport]` interfaces. That means the app **consumes** them. Nothing in the assembly **implements** them or is marked COM-visible.
- A thumbnail handler must be an in-process COM DLL exporting `DllGetClassObject` / `DllCanUnloadNow` (and `DllRegisterServer` / `DllUnregisterServer` for self-registration). An exe cannot be that.
- Microsoft discourages managed (.NET) in-process shell extensions because of CLR version conflicts inside Explorer and `dllhost.exe` [Inferred]. The handler should be native.

**Fix:** a native DLL. See 5.2 and 5.4.

### F-02 (Critical): depends on Ardfry's COM classes [Verified]
`TryArdfryThumbnail` (the first thing `ExtractThumbnail` calls) does:
`Type.GetTypeFromCLSID({43341271-304D-40f0-81BB-EBE341997DF2})` → `Activator.CreateInstance` → `SHCreateStreamOnFileEx` → `IInitializeWithStream.Initialize` → `IThumbnailProvider.GetThumbnail`.

`{43341271-…}` is the CLSID of **Ardfry's PSD Thumbnail Provider**. The installer registers it under `InprocServer32` pointing at `ardfryPSDcodec.dll`. Your app has no equivalent entry. So on a machine without PSD Codec this returns null and the code falls through. On a machine with PSD Codec, the real work is done by PSD Codec, not your app.

**Fix:** own CLSID (freshly generated GUID), own DLL.

### F-03 (Critical): `.bat` does not create the thumbnail binding [Verified]
The two lines in the `.bat` do this:
1. `HKLM\…\Shell Extensions\Approved` gets a value named after Ardfry's CLSID. That is only an allow-list entry. It does not register anything.
2. `HKCR\.psd` default value set to `Photoshop.Image`.

Missing: `HKCR\.psd\ShellEx\{e357fccd-a995-4576-b01f-234630154e96}` = *handler CLSID*. That key is the actual thumbnail binding. PSD Codec's installer writes it for `.psd`, `.psb`, `.pdd` (and under `SystemFileAssociations\.psd\ShellEx` too). The `.bat` never does, so the script cannot enable thumbnails even if the DLL were present.

### F-04 (High): file-association damage [Verified + Inferred]
`reg add "HKCR\.psd" /ve /d "Photoshop.Image" /f` forcibly overwrites whatever ProgID `.psd` had (for example the one Photoshop or Affinity created). The `Photoshop.Image` ProgID is created by PSD Codec's MSI, not by the `.bat`. Without it, `.psd` points at a ProgID with no `shell\open` command [Inferred]. Result: broken "Open with" behaviour, and the original value is lost because the script does not save it.

### F-05 (High): `.bat` robustness [Verified]
- Writes to HKLM and HKCR, so it needs administrator rights. There is no elevation check, no `echo`/`pause`, no `errorlevel` handling. A non-admin run fails silently.
- No undo script.
- Mixed line endings (line 1 LF, line 2 CRLF).

### F-06 (High): no cache refresh [Verified]
Explorer caches "no thumbnail" results in `%LocalAppData%\Microsoft\Windows\Explorer\thumbcache_*.db`. The `.bat` does not clear it and does not broadcast `SHChangeNotify(SHCNE_ASSOCCHANGED)`. (The exe does call `SHChangeNotify` with the right constants, but only from the Repair button.) Even a correct registration will look broken until the cache is rebuilt.

### F-07 (High): fallback is not a decoder [Verified]
`PsdReader.ExtractThumbnail` walks the PSD header (`8BPS`, version 1 or 2), skips the colour-mode section, and scans Image Resources (`8BIM`) for ID **1036** (`0x040C`) or **1033** (`0x0409`). It reads the 28-byte header and loads the rest as a JPEG.

This is a sound reader of the *embedded preview*, but:
- Many PSDs have no embedded thumbnail, or one capped at about 160 px. Larger tiles (Medium, Large, Extra Large icons) will be blurry or blank.
- It never decodes the **Image Data section** (the flattened composite), which is what real codecs use for sharp thumbnails.
- It does not validate the thumbnail header (`format == 1`, width/height sanity).
- It does not handle ICC profiles or colour modes.

### F-08 (Medium): bitness mismatch [Verified + Inferred]
The exe is x86. In a 32-bit process, `GetTypeFromCLSID` reads the 32-bit registry view. PSD Codec ships separate x64 and x86 MSIs (x64 MSI installs under `ProgramFiles64Folder`). On a 64-bit machine with only the x64 build installed, the 32-bit viewer cannot load the 64-bit DLL, so the Ardfry path returns null [Inferred]. Explorer on 64-bit Windows is 64-bit and needs the x64 handler.

### F-09 (Medium): "Repair" button side-effects [Verified]
`BtnRepairRegistry_Click` writes six default values under `HKCU\Software\Classes`:

| Key | Value |
|---|---|
| `Applications\Photoshop.exe\ShellEx\{e357fccd-…}` | `{43341271-…}` (Ardfry) |
| `Applications\Photoshop.exe\ShellEx\{BB2E617C-…}` | `{74113A70-…}` (Ardfry) |
| `.psd\ShellEx\{e357fccd-…}` | `{43341271-…}` |
| `.psd\ShellEx\{BB2E617C-…}` | `{74113A70-…}` |
| `SystemFileAssociations\.psd\ShellEx\{e357fccd-…}` | `{43341271-…}` |
| `Photoshop.Image\ShellEx\{e357fccd-…}` | `{43341271-…}` |

Problems:
- The two `Applications\Photoshop.exe\ShellEx` keys do nothing for file thumbnails.
- HKCU wins over HKLM. If PSD Codec is later uninstalled or replaced, these orphaned keys keep pointing at dead CLSIDs and **mask any new handler** registered machine-wide.
- It shows "Windows Shell refreshed successfully!" unconditionally. No check that the CLSID or its `InprocServer32` exists.

### F-10 (Medium): fake status [Verified]
The label "Explorer Integration: Active" is static. The assembly imports only `Registry.CurrentUser`, `CreateSubKey` and `SetValue`. It never calls `OpenSubKey` or `GetValue`, so it cannot know the real state.

### F-11 (Medium): scope gaps [Verified]
Only `*.psd` is scanned and registered. No `.psb`, no `.ai`, which was the stated goal.

### F-12 (Low): polish [Verified]
- Hard-coded quick location `D:\ (My Kitchen)`.
- Version info `0.0.0.0`, no company or copyright, not Authenticode-signed (security directory size 0). It does have a manifest.

### F-13 (Low): alpha lost [Verified]
`Image.FromHbitmap` drops alpha, so transparent PSDs show on a solid background.

### F-14 (Info): what is already correct
- COM interface IDs: `IThumbnailProvider` = `e357fccd-a995-4576-b01f-234630154e96`, `IInitializeWithStream` = `b824b49d-22ac-4161-ac8a-9916e8fa3f7f`.
- `SHChangeNotify(0x08000000, 0, 0, 0)` (`SHCNE_ASSOCCHANGED`) is the right call.
- The PSD resource walker, including even-padding of the Pascal name and data, is correct and is a good basis for the **fast path** in the new decoder.
- Resources 1033/1036 and the 28-byte thumbnail header layout are right.

### F-15 (Legal): do not reuse Ardfry identifiers
PSD Codec is commercial software (its guide describes a 15-day trial, then a paid licence). The current build reuses Ardfry's CLSIDs and writes an "Approved" description naming "Ardfry Imaging, LLC". The new project must:
- generate **new GUIDs** for every class,
- never register or reference Ardfry's CLSIDs, DLL or branding,
- never bundle or redistribute Ardfry's DLL,
- implement decoding from the **public Adobe Photoshop file format documentation**, not from decompiling PSD Codec.

---

## 3. Why it fails on a clean PC (failure chain)

1. User runs the `.bat`: Ardfry's CLSID is allow-listed, `.psd` default ProgID is rewritten. No handler binding is created.
2. Explorer asks "who makes thumbnails for `.psd`?" and finds no `ShellEx\{e357fccd-…}` entry.
3. Even if the Repair button was used: the binding points at `{43341271-…}`, which has no `InprocServer32` on this machine.
4. Explorer shows the generic icon and caches that result.
5. The viewer app tries the same missing COM class, then falls back to the embedded JPEG. This works only for PSDs that happen to contain one, and only inside the viewer window, never in Explorer.

---

## 4. How PSD Codec actually works (reference architecture)

All of this comes from the installer's metadata [Verified]. It is for understanding the **shape** of a working solution. Do not copy identifiers.

**Delivery**
- Inno Setup wrapper that contains two MSIs (x64 and x86). Per-machine install (`ALLUSERS=1`). A custom action calls `SHChangeNotify` after install, and the guide recommends a reboot.
- A single **native** DLL, `ardfryPSDcodec.dll` (about 766 KB x64 / 634 KB x86). Exports `DllGetClassObject`, `DllCanUnloadNow`, `DllRegisterServer`, `DllUnregisterServer`. Imports only core Win32 DLLs (`ole32`, `shlwapi`, `gdi32`, …), so there is **no .NET dependency**.
- A small settings exe (`PSDCodecPreferences.exe`).

**What it registers (one DLL, many CLSIDs)**

| Role | Mechanism |
|---|---|
| Explorer thumbnails (Vista+) | CLSID with `InprocServer32` → DLL (`ThreadingModel` Both/Apartment); bound via `.psd\ShellEx\{e357fccd-…}` and `SystemFileAssociations\.psd\ShellEx\{e357fccd-…}` |
| Legacy thumbnails (XP-era) | `IExtractImage` binding `…\ShellEx\{BB2E617C-0920-11d1-9A0B-00C04FC2D6C1}` |
| Preview Pane, Photo Viewer, File dialogs | **WIC decoder** registered in the WIC decoder category with byte patterns `38 42 50 53 00 01` (PSD) and `38 42 50 53 00 02` (PSB), i.e. `8BPS` plus version, at offset 0 |
| Metadata and search | Property handler under `PropertySystem\PropertyHandlers\.psd` (and `.psb`, `.pdd`) plus `ExtendedTileInfo`, `FullDetails`, `PreviewDetails`, `InfoTip` |
| Allow-list | `HKLM\…\Shell Extensions\Approved\{CLSID}` for each handler |
| File type hygiene | `PerceivedType=Image`, `Content Type=image/psd`, `Kindmap`, ProgIDs |
| Other formats | Separate CLSIDs for AI, EPS, INDD, AEP/AET, PDF thumbnails. Its guide says these use **embedded preview images** where present |
| Overlay | `TypeOverlay` property so the file type badge appears on the thumbnail |

**Takeaways for the new app**
- A thumbnail codec is **a registered COM DLL**, not an app.
- One DLL can host multiple handlers, but each needs its own CLSID.
- The WIC decoder and property handler are what upgrade it from "thumbnails" to "full Windows integration". They are optional Phase 4 items.
- PSD Codec's AI/EPS/PDF support relies on embedded previews. A free app can match that and beat it (5.6).

---

## 5. Required solution: build specification

### 5.1 Product definition
Free, open-source Explorer thumbnail provider for **PSD / PSB** (Phase 1-2) and **AI** (Phase 3). Optional viewer app is secondary.

Non-goals for v1: layer-by-layer rendering, editing, converting, cloud features.

### 5.2 Tech stack (decision)
- **Shell extension and decoder core: native C++20 (MSVC) or Rust (`windows` crate).** Not .NET Framework, not Electron, not Python.
- Use Windows built-ins where possible: **WIC** (`IWICImagingFactory`) for JPEG decode and high-quality scaling; **Windows.Data.Pdf** (WinRT) for AI (5.6); **WCS/ICM** or WIC colour transform for ICC.
- Installer: **WiX Toolset v4 MSI**. Build targets: **x64 and ARM64** (x86 optional).
- Licence: MIT or Apache-2.0.

### 5.3 Repository layout
```
/src/core        psd/ai parsing + decoding, no COM, no Windows UI, unit-testable
/src/shellext    COM in-proc DLL: class factory, ThumbnailProvider, exports
/src/installer   WiX project (registry, DLL, SHChangeNotify custom action)
/src/diag        command-line diagnostic + repair tool
/src/viewer      (optional) separate gallery app, consumes the DLL
/tests           corpus, golden images, fuzz targets
/docs            format notes, registry spec, test report
```

### 5.4 COM server spec
- Exports: `DllGetClassObject`, `DllCanUnloadNow`, `DllRegisterServer`, `DllUnregisterServer`.
- Class implements `IInitializeWithStream` and `IThumbnailProvider`. Use the **stream**, never a file path (needed for OneDrive/cloud and for sandboxing).
- `ThreadingModel = Apartment`. Do **not** set `DisableProcessIsolation`, so the handler runs in the `dllhost.exe` surrogate.
- `GetThumbnail(UINT cx, HBITMAP* phbmp, WTS_ALPHATYPE* pdwAlpha)`:
  - `cx` is the max edge length. Preserve aspect ratio.
  - Return a **32-bpp BGRA DIB section** and set `WTSAT_ARGB` when there is alpha. Premultiply alpha, and verify visually on light and dark Explorer themes.
  - Return a failure `HRESULT` for unsupported or corrupt files. Never return a blank bitmap.
- Reliability rules: catch all exceptions at the COM boundary (no exception may cross it); no UI, no network, no file writes; strict time and memory budgets; no global mutable state; thread-safe.
- Generate **fresh GUIDs** for the class and for any extra handlers, and keep them constant forever.

### 5.5 PSD / PSB decoder pipeline (fast path first)
Read from the `IStream` with seeks. Do not load the whole file. On OneDrive and network drives this decides whether Explorer feels instant.

1. **Header (26 bytes):** signature `8BPS`; version 1 = PSD, 2 = PSB; channels; height; width; depth (1/8/16/32); colour mode. Validate everything.
2. **Colour Mode Data section** (length-prefixed): skip (keep palette for Indexed).
3. **Image Resources section** (4-byte length): iterate `8BIM` blocks.
   - **1036** (RGB JPEG thumbnail, supersedes 1033) and **1033** (legacy). Validate `format == 1`, dimensions, sizes. Decode with WIC. If the embedded thumbnail is at least about as large as the requested `cx`, use it and **stop**.
   - **1060** XMP: may contain `xmp:Thumbnails` with a base64 JPEG. Secondary fast path.
   - **1039** ICC profile (for colour conversion).
4. **Layer and Mask Information:** skip using its length (**8 bytes in PSB**, 4 in PSD). If the layer count is negative, the first alpha channel holds the merged transparency.
5. **Image Data section (composite):** 2-byte compression: `0` raw, `1` RLE (PackBits; row byte counts are 2 bytes in PSD, **4 bytes in PSB**), `2` ZIP, `3` ZIP with prediction. Data is **planar** (all of channel 0, then channel 1, …).
6. Support modes: Bitmap, Grayscale, Indexed, RGB, CMYK, Lab, Multichannel, Duotone. Depths: 8 and 16 required for v1; 32-bit float after that. Convert to sRGB using the embedded ICC profile when present.
7. Downscale with a high-quality WIC scaler to `cx`, then output the DIB.

Known limitation to document: a PSD saved without **Maximize Compatibility** may have a placeholder composite [Inferred, verify against real files]. Fall back to the embedded thumbnail in that case. Full layer compositing is a later optional phase.

### 5.6 Illustrator (.ai) pipeline [needs validation on real files]
1. XMP packet inside the file: `xmp:Thumbnails` (`xmpGImg:image`, base64 JPEG). Fast path, same idea PSD Codec uses.
2. Modern `.ai` files saved with "Create PDF Compatible File" are PDF-based: render **page 1** with `Windows.Data.Pdf`. This is how the free app can exceed PSD Codec's embedded-preview-only approach. Test that WinRT works inside the `dllhost.exe` surrogate; if not, move the renderer to a helper process.
3. If neither works, return failure (generic icon). Do not fake it.

Out of scope: pre-CS PostScript-only `.ai`.

### 5.7 Registration spec (installer, not a `.bat`)
Use **HKLM\Software\Classes** (not `HKCR` writes), and write both 64-bit and 32-bit views only for architectures actually shipped.

| Key | Value |
|---|---|
| `CLSID\{NEW-GUID}` | friendly name |
| `CLSID\{NEW-GUID}\InprocServer32` | (default) = full DLL path; `ThreadingModel` = `Apartment` |
| `.psd\ShellEx\{e357fccd-a995-4576-b01f-234630154e96}` | (default) = `{NEW-GUID}` |
| `SystemFileAssociations\.psd\ShellEx\{e357fccd-…}` | same |
| Same two keys for `.psb` (and `.ai` when Phase 3 lands) | same |
| `HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Shell Extensions\Approved` | value `{NEW-GUID}` = your own description |

Rules:
- **Never** overwrite the `.psd` default value or create/alter ProgIDs.
- **Detect competing handlers** (PSD Codec, Adobe, Affinity) before binding. Report them. Back up what you replace and restore it on uninstall.
- No HKCU "repair" keys. If a per-user install mode is offered, use HKCU consistently and say so.
- After install: `SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, 0, 0)`. Offer an optional "clear thumbnail cache" step with consent. No reboot should be needed.

### 5.8 Installer and distribution
- WiX v4 MSI, per-machine, x64 + ARM64. Proper uninstall that removes every key it created.
- Version info, product name, publisher and licence in the MSI and DLL resources.
- Code signing (unsigned software triggers SmartScreen). Open-source options exist, for example SignPath Foundation, subject to approval.
- GitHub Releases with checksums, plus a plain-language privacy statement (no telemetry).

### 5.9 Diagnostics and repair tool (replaces the `.bat` and the Repair button)
Command-line `psdthumb-diag.exe` (and `/repair`) must:
1. Check for administrator rights where needed, and say so clearly.
2. **Read** the real state: for each extension, who owns `ShellEx\{e357fccd-…}` in HKLM and HKCU; does the CLSID exist; does `InprocServer32` point at a file that exists; is that file the right architecture.
3. Detect competing handlers and orphaned HKCU keys.
4. Run the decoder on a sample file and print timing and the decode path used.
5. Repair: re-register, `SHChangeNotify`, optional thumbnail cache clear (`thumbcache_*.db`) and Explorer restart, each behind a confirmation.
6. Exit codes and readable messages. Never print "success" without verifying.

### 5.10 Viewer app (optional, separate project)
- Consumes the shell DLL (or the core library); no registry hacks.
- Real integration status from the diag logic. No hard-coded folders. Alpha-aware checkerboard. Async loading with cancellation. PSD and AI. Proper version info.

### 5.11 Security and robustness
- Treat every file as hostile. PSB dimensions go up to 300,000 px: guard `width * height * channels * depth` against overflow; cap allocations and decode size; reject lengths that exceed the stream.
- Add **fuzz targets** (libFuzzer or AFL++) for the header parser, resource walker and RLE/ZIP decoders. Run in CI.
- No recursion on file-controlled structures. Time budget per call. No blocking calls on UI threads.

### 5.12 Test plan and acceptance criteria
**Corpus:** PSD with thumbnail; PSD without thumbnail; Maximize Compatibility off; 8/16-bit; RGB, CMYK, Grayscale, Lab, Indexed, Duotone; transparent background; ICC-tagged; PSB; very large; truncated; corrupted; AI with and without PDF compatibility; files from Affinity, Photopea and Figma exports; files in a OneDrive folder.

**Environments:** Windows 10 22H2, Windows 11 (current), x64 and ARM64, clean VM, and a VM with PSD Codec installed.

**Acceptance (all must pass):**
1. On a clean VM with no PSD Codec, installing the MSI makes PSD thumbnails appear in Explorer at all sizes **without reboot** (after the install-time notification).
2. Uninstall removes all keys and the generic icon returns.
3. `.psd` default association and "Open with" are untouched.
4. Corrupt files never crash Explorer or `dllhost.exe`.
5. Thumbnail for a typical 100 MB PSD appears in under about 1 s on the fast path.
6. Diagnostic tool reports state that matches what Explorer actually does.
7. No Ardfry CLSID, string, or binary appears anywhere in the repo or the build output.

---

## 6. Phased roadmap

| Phase | Deliverable | Done when |
|---|---|---|
| 0 | Stop shipping `.bat` + Repair button; run the cleanup in section 7 on dev machines | Dev machine has no leftover Ardfry bindings |
| 1 | Native DLL + MSI: embedded-thumbnail path for PSD/PSB, diag tool | Acceptance 1-4, 6, 7 on the embedded-thumbnail corpus |
| 2 | Composite decoder (all modes, 8/16-bit, ICC, PSB) | PSDs without embedded thumbnails render correctly |
| 3 | AI support (XMP thumbnail, then PDF render) | AI corpus passes |
| 4 | Optional parity with PSD Codec: WIC decoder, Preview Pane, property handler | Preview Pane and Photo Viewer work |
| 5 | Optional: more formats (for example Affinity) | Research spike first: confirm what preview data those files embed. Design the codec layer so new formats plug in |

---

## 7. Cleanup of what the current build already changed (dev machines)

Run in an **elevated** Command Prompt. Review each line first.

```bat
:: 1. Orphaned per-user keys written by the Repair button
reg delete "HKCU\Software\Classes\Applications\Photoshop.exe\ShellEx" /f
reg delete "HKCU\Software\Classes\.psd\ShellEx" /f
reg delete "HKCU\Software\Classes\SystemFileAssociations\.psd\ShellEx" /f
reg delete "HKCU\Software\Classes\Photoshop.Image\ShellEx" /f

:: 2. Check what .psd currently points to BEFORE changing anything
reg query "HKCR\.psd" /ve

:: 3. Allow-list entry added by the .bat
::    (Only delete this if PSD Codec is NOT installed. PSD Codec's own installer creates it.)
reg delete "HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Shell Extensions\Approved" /v "{43341271-304D-40f0-81BB-EBE341997DF2}" /f
```

Restoring the original `.psd` default value: the `.bat` did not save it. Reinstalling Photoshop (or choosing the program again via "Open with → Always") rewrites it. If you want an exact restore, check another machine with the same software.

Warning: steps 1 and 3 delete keys. If PSD Codec is installed and you want to keep it, skip step 3 and reinstall PSD Codec afterwards to be safe.

---

## 8. Ready-to-paste brief for Antigravity

> **Task:** Replace the existing "PSD Thumbnail Pro" prototype with a proper Windows Explorer thumbnail provider for PSD/PSB (later AI). Read `PSD_Thumbnail_Audit_Report.md` fully first.
>
> **Why the prototype fails:** it is a WinForms .exe, not a COM shell extension; it reuses Ardfry's CLSIDs; its `.bat` never creates `.psd\ShellEx\{e357fccd-a995-4576-b01f-234630154e96}`; it overwrites the `.psd` ProgID; it only reads embedded JPEG thumbnails.
>
> **Build:** a native (C++20/MSVC or Rust) in-process COM DLL implementing `IInitializeWithStream` + `IThumbnailProvider`, with exports `DllGetClassObject`, `DllCanUnloadNow`, `DllRegisterServer`, `DllUnregisterServer`; `ThreadingModel=Apartment`; process isolation left ON; its **own freshly generated GUID**.
>
> **Decode:** stream-based. Fast path = embedded thumbnail (resource 1036/1033, then XMP resource 1060). Then composite decode from the Image Data section (Raw/RLE/ZIP/ZIP-prediction; PSB 8-byte lengths and 4-byte RLE counts; Gray/RGB/CMYK/Lab/Indexed/Duotone; 8/16-bit; ICC resource 1039). Scale with WIC; return a 32-bpp BGRA DIB with `WTSAT_ARGB`. Never throw across the COM boundary.
>
> **Register:** via WiX v4 MSI under `HKLM\Software\Classes`: `CLSID\{GUID}\InprocServer32`, `.psd`/`.psb\ShellEx\{e357fccd-…}`, `SystemFileAssociations\.psd\ShellEx\{e357fccd-…}`, Approved list. **Do not** touch the `.psd` default value or ProgIDs. Detect and report competing handlers. Call `SHChangeNotify(SHCNE_ASSOCCHANGED)` after install. Clean uninstall.
>
> **Also build:** a diagnostic/repair CLI that reads real registry state, verifies the DLL path and architecture, runs a test decode, and only then reports success.
>
> **Hard rules:** no Ardfry CLSIDs, strings or binaries; implement from public Adobe format documentation; fuzz the parsers; no telemetry; MIT/Apache-2.0.
>
> **Acceptance:** section 5.12 of the report.

---

## 9. What this audit could not verify
- Nothing was run on Windows. Items marked [Inferred] (ProgID breakage, bitness behaviour, `dllhost` WinRT behaviour, placeholder composite when Maximize Compatibility is off) need confirmation on a real machine.
- PSD Codec's decoding internals were deliberately not analysed. Only its installer metadata and public export names were used to understand how a handler is registered.
- AI and Affinity embedded-preview formats were not tested. Treat 5.6 and Phase 5 as design hypotheses until validated against real files.
