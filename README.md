# GamePilot

A lightweight Windows gaming control panel built with C++ and the Win32 API.

GamePilot aims to bring gaming settings and system monitoring into one native desktop app, with low RAM usage and minimal background activity.

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

## Status

Initial repository setup. The C++ application has not been implemented yet.

Build and development instructions will be added with the first implementation.
