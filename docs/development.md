# Development

## Toolchain

The supported local setup targets Windows x64 and uses:

| Tool | Pinned version | Purpose |
| --- | --- | --- |
| LLVM-MinGW | 20261006 / Clang 23.1.3 | Compiler, LLD linker, libc++, MinGW-w64 Win32 headers/import libraries, LLDB, clangd, clang-format, clang-tidy |
| CMake | 4.4.4 | Target configuration, presets, compilation database, CTest |
| Ninja | 1.13.2 | Incremental builds |

`tools/toolchain.lock.json` is the source of truth for downloads and SHA-256 hashes. `scripts/setup.ps1` checks each archive before extracting it. Packages are installed under `%LOCALAPPDATA%\Programs\GamePilotTools` with versioned directories. Completed installs are reused; download archives are removed after installation. No administrator access or permanent PATH changes are needed.

An interrupted extraction is reported rather than overwritten. Move the named incomplete package directory aside and rerun setup. A failed download can simply be retried. To update tools, edit the lock file's version, directory, upstream URL and SHA-256 together, rerun setup, and reconfigure from a fresh build directory. Older tool directories are retained until you remove them.

## Build, format, and test

Activate the toolchain in the current PowerShell session:

```powershell
. .\scripts\enter-dev-shell.ps1
cmake --preset debug
cmake --build --preset debug
ctest --preset debug

cmake --preset release
cmake --build --preset release
ctest --preset release

cmake --build --preset debug --target format-check
cmake --build --preset debug --target format
$sources = Get-ChildItem src,tests -Recurse -Filter *.cpp | ForEach-Object FullName
clang-tidy -p build/debug $sources
```

The build wrapper also activates the environment and works from any working directory. On systems whose script policy blocks local scripts, invoke it with the process-scoped `-ExecutionPolicy Bypass` command shown in the readme; no policy is changed persistently.

The smoke test runs `gamepilot.exe --smoke-test`: it creates a real hidden Win32 window, queues an exit command, runs the message loop, and checks for a successful exit. It does not load recovery data, create a tray icon, or touch real power settings. It does not verify pixels, interactions, or multi-monitor DPI transitions; those require a visible manual check.

The `gaming_mode` test uses fake power settings to cover exact restoration, repeated toggles, durable save before mutation, partial enable rollback, failed restore/retry, restart recovery, external changes, plan switches, ignored writes, and journal cleanup failures. Disk journal tests use a private temporary folder to cover atomic replacement and malformed/truncated records. Tests never modify machine power settings.

CI uses the same pinned setup, builds Debug and Release, checks formatting and static analysis, and runs both tests on Windows. Workflow dependencies are pinned to commit SHAs.

## Editor and debugger

The generated `gamepilot.code-workspace` selects the installed clangd explicitly. The shared `.vscode` folder provides extension recommendations, build tasks, and CodeLLDB launch settings. If you open the folder directly instead, first activate the development shell and launch VS Code from it so clangd is on PATH.

Zed has build/run tasks and a CodeLLDB configuration in `.zed/`. Setup creates an ignored `.zed/settings.json` with the local clangd path; an existing file is preserved. After upgrading LLVM, update that local path or remove the generated settings file and rerun setup. Zed manages its own CodeLLDB adapter on first use. Editor debugger adapters may require their own download; the toolchain's command-line LLDB is already installed.

Build Debug once to generate `build/debug/compile_commands.json`; `.clangd` points to that database. CodeLLDB supplies the VS Code debugging adapter. Debug builds retain symbols; F5 builds before launching. These editor configurations do not require the Microsoft C/C++ extension or Visual Studio.

Command-line debugging is available after activating the development shell:

```powershell
lldb .\build\debug\gamepilot.exe
# In LLDB:
# breakpoint set --name wWinMain
# run
# continue
```

## Source layout

```text
src/main.cpp                    Process entry point and final error reporting
src/core/gaming-mode.*           Mode state, verification, rollback and recovery orchestration
src/win32/application.hpp        Native application entry interface
src/win32/application.cpp        Dark window, native controls, tray, lifetime and message loop
src/win32/power-settings.*       Windows power APIs and durable recovery storage
tests/gaming-mode-tests.cpp      Failure-path tests and real disk journal tests
resources/                      Manifest and executable version information
cmake/toolchains/               LLVM-MinGW compiler selection
scripts/                        Setup, environment activation, build wrapper
tools/toolchain.lock.json        Pinned tool versions and integrity hashes
.vscode/                        Shared editor tasks and debugging configuration
.zed/                           Zed build/run tasks and debugging configuration
.github/workflows/              Windows build verification
```

## Conventions

- C++23, with extensions disabled. Use supported standard features deliberately; this is not a C++26 preview project.
- Lowercase, hyphen-separated filenames where applicable. `CMakeLists.txt`, `CMakePresets.json`, and optional `CMakeUserPresets.json` keep the case required by CMake.
- Target-scoped CMake settings. Strict compiler warnings are errors.
- Scoped resource ownership; no owning raw pointers or manual `new`/`delete`. Win32 handle values and callback pointers remain at the platform boundary.
- Use `std::expected` for expected startup failures. Window callbacks are `noexcept`; exceptions must not escape through Windows.
- Unicode Windows APIs, per-monitor DPI awareness, and standard-user execution.
- Block on the Windows message queue while idle. No rendering loop. While settings are owned, verify every five seconds as a fallback for external changes; power notifications also trigger verification. The timer stops when no recovery is pending. Redraw only when status changes.
- Keep future feature logic independent of window rendering. Add abstractions when an actual feature needs them.
- Do not commit or push changes without the user's explicit permission.

## Mode architecture and recovery

The UI invokes a `gaming_mode` controller through enable/disable/status operations. The controller depends on small `power_settings` and `recovery_store` interfaces, with Windows implementations in a separate static library. No Windows types enter the core. Future FPS/GPU features can join the controller as separate operations with their own snapshots and rollback; don't add them to window callbacks or imply that they are implemented now.

Before the first mutation, write a versioned recovery record to a sibling temporary file, flush it, close it, then atomically rename it with write-through. After any failed enable, attempt restoration. Only delete the record after successful restoration and read-back. Failed restore or deletion leaves recovery available for retry. Restoring a non-current plan never intentionally selects it; Windows requires reactivating the current scheme to apply its updated values. A concurrent external plan switch between checking and reactivating is not atomic in the Windows API.

Startup restores pending settings before showing normal status. A session-local mutex prevents duplicate UI instances; launching again requests the existing window. Closing hides to tray; an unavailable tray falls back to exit-and-restore. Explorer restart recreates the icon. Normal exit refuses to quit after a restoration error. During shutdown restoration is best effort and never vetoes shutdown; abrupt termination requires the next launch to recover. There is no service or scheduled recovery task.

Manual verification: record `powercfg /qh SCHEME_CURRENT SUB_BUTTONS LIDACTION`, toggle on (both indices should become 0), toggle off (both originals should return), exercise tray close/reopen and Exit and restore, and check that a forced stop while enabled is recovered on relaunch. Physical lid-close behavior and multi-monitor DPI changes require hardware testing. Avoid running another lid-setting utility during testing.

The executable statically links the toolchain C++ runtime and uses Windows system DLLs. This avoids shipping compiler runtime DLLs, but increases executable size. Binary size is not runtime RAM usage. LLVM-MinGW libraries have a different ABI from MSVC C++ libraries; future binary dependencies must be chosen accordingly.
