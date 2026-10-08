# GamePilot

A lightweight Windows gaming control panel built with C++ and the Win32 API.

GamePilot aims to bring gaming settings and system monitoring into one native desktop app, with low RAM usage and minimal background activity.

## Current milestone

A native Windows **Hello, world!** window, built with **C++23 and Win32**. The first milestone establishes the toolchain, resource ownership, error handling, DPI awareness, formatting, build presets, and CI. Gaming settings and monitoring are not implemented in this repository yet.

## Quick start

Windows 10/11 x64, PowerShell 5.1 or later. No Visual Studio IDE, MSVC Build Tools, separate Windows SDK, MSYS2, or browser runtime is required.

From a PowerShell terminal in the repository:

```powershell
# One-time, per-user installation of checksum-verified, pinned tools.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\setup.ps1

# Build and open the native window.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\build.ps1 -Preset debug -Run

# Optimized build.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\build.ps1 -Preset release
```

Outputs: `build/debug/gamepilot.exe` and `build/release/gamepilot.exe`.

**VS Code:** open the generated `gamepilot.code-workspace` and install its recommended **clangd** and **CodeLLDB** extensions. Use **Ctrl+Shift+B** to build and **F5** to debug. The workspace file contains local tool paths and is ignored by Git. No editor is installed by the setup script.

**Zed:** open the repository folder. Use **task: spawn** for the build/run tasks and **debugger: start** for the debug configuration. Setup generates local clangd settings so Zed can use the installed compiler tools.

See [development setup](docs/development.md) for direct commands, tool versions, checks, and conventions.

## Planned features

- Turn Gaming Mode on and off from the dashboard or system tray.
- Keep the laptop running when its lid is closed, both on battery and plugged in.
- Restore previous settings when Gaming Mode is disabled.
- Show a clear indicator when Gaming Mode is active, inactive, or needs attention.
- Monitor RAM and CPU usage while the dashboard is open.
- Add configurable gaming profiles and settings over time.

## Design priorities

- Native C++ and Win32 UI, without an embedded browser or WebView.
- Low memory usage and minimal CPU activity, especially while in the tray.
- Pause dashboard rendering and unnecessary monitoring when hidden.
- Save original settings before changes and support recovery after interruptions.
- Keep Windows integration, feature logic, and UI code separate.
