# GamePilot

A lightweight Windows gaming control panel built with C++ and the Win32 API.

GamePilot aims to bring gaming settings and system monitoring into one native desktop app, with low RAM usage and minimal background activity.

## Current milestone

A native dark control panel and system tray utility, built with **C++23 and Win32**. Gaming Mode currently changes one setting: closing the laptop lid does nothing, on both battery and plugged-in power.

## Gaming Mode

- **Turn on gaming mode** saves the current plan's original lid actions, applies Do nothing to both power sources, and verifies the result.
- **Turn off gaming mode** restores the exact original values. An original Do nothing setting stays Do nothing.
- **Close (X)** hides the window; the tray keeps running. Click the tray icon or launch the executable again to reopen it.
- **Exit and restore** restores settings and quits. If restoration fails, the app stays open so you can retry.
- The window, taskbar icon, and tray show **ON** (green), **OFF** (gray), or **NEEDS ATTENTION** (amber). Right-click the tray icon for controls. Windows may initially place the icon in the hidden-icons area.

If the active power plan or lid settings change externally, GamePilot shows Needs attention. Restore, then enable again to apply the mode to the new plan. GamePilot does not continuously overwrite other apps' changes.

Originals are saved atomically under `%LOCALAPPDATA%\gamepilot\lid-recovery.txt` before modifying Windows. Normal exit and sign-out/shutdown attempt restoration. After a forced stop or crash, changes remain until the next launch, which restores the saved values and starts with Gaming Mode off. Invalid recovery files are preserved and reported rather than overwritten.

This controls **lid close only**. Idle sleep, explicit Sleep/Shut down, and critical-battery actions still apply. Windows policy can deny power-setting changes; failures appear in the status panel. Don't run the old GamingMode tray utility alongside GamePilot.

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

- Monitor RAM and CPU usage while the dashboard is open.
- Explore frame-rate limits, GPU controls, and configurable profiles as separate features.

## Design priorities

- Native C++ and Win32 UI, without an embedded browser or WebView.
- Low memory usage and minimal CPU activity, especially while in the tray.
- Pause dashboard rendering and unnecessary monitoring when hidden.
- Save original settings before changes and support recovery after interruptions.
- Keep Windows integration, feature logic, and UI code separate.
