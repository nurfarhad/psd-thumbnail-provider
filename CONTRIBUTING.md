# Contributing to PSD & AI Native Windows Shell Thumbnail Provider

Thank you for your interest in contributing!

## Code of Conduct

Please be respectful, constructive, and considerate when interacting with maintainers and other contributors.

## Building from Source

To build the project locally on Windows:
1. Ensure LLVM-MinGW or Clang is installed (e.g. via `winget install MartinStorsjo.LLVM-MinGW.UCRT`).
2. Run `build.bat` from the root directory.
3. The compiled 64-bit binaries will be output to the `bin/` directory.

## Testing & Diagnostics

Use `bin\psdthumb-diag.exe` to test your changes:
- Check registry and Shell status: `bin\psdthumb-diag.exe /status`
- Test direct stream decode: `bin\psdthumb-diag.exe /decode <file.psd> [maxEdge] [out.bmp]`
- Test COM interface: `bin\psdthumb-diag.exe /test-com <file.psd> [maxEdge] [out.bmp]`
- Test Windows Explorer Shell pipeline: `bin\psdthumb-diag.exe /test-shell <file.psd> [maxEdge] [out.bmp]`

## Pull Request Guidelines

1. Ensure the code compiles cleanly with `-DUNICODE -D_UNICODE -std=c++20 -O2`.
2. Do not introduce dependencies on third-party runtime libraries.
3. Ensure no regressions in memory management, bounds validation, or registry safety.
