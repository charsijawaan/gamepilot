#include "win32/application.hpp"
#include "core/gaming-mode.hpp"
#include "win32/power-settings.hpp"

#include <dwmapi.h>
#include <shellapi.h>

#include <array>
#include <cmath>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>

namespace gamepilot::win32 {
namespace {

constexpr auto window_class_name = L"GamePilot.MainWindow";
constexpr UINT tray_message = WM_APP + 1;
constexpr UINT show_message = WM_APP + 2;
constexpr UINT verify_message = WM_APP + 3;
constexpr UINT toggle_id = 101, exit_id = 102, open_id = 103;
constexpr UINT_PTR verify_timer = 1;
constexpr COLORREF background = RGB(20, 24, 32), surface = RGB(30, 36, 47);
constexpr COLORREF foreground = RGB(232, 237, 245), muted = RGB(164, 177, 196);
constexpr COLORREF accent = RGB(96, 222, 168), warning = RGB(255, 195, 104);

auto last_error() noexcept -> std::error_code {
    const auto code = GetLastError();
    return {static_cast<int>(code == 0 ? ERROR_GEN_FAILURE : code), std::system_category()};
}
struct gdi_deleter {
    void operator()(void* object) const noexcept { DeleteObject(object); }
};
template <typename T> using unique_gdi = std::unique_ptr<std::remove_pointer_t<T>, gdi_deleter>;
struct icon_deleter {
    void operator()(HICON icon) const noexcept { DestroyIcon(icon); }
};
using unique_icon = std::unique_ptr<std::remove_pointer_t<HICON>, icon_deleter>;
struct handle_deleter {
    void operator()(void* handle) const noexcept { CloseHandle(handle); }
};

auto widen(const std::string& text) -> std::wstring {
    if (text.empty()) {
        return {};
    }
    const auto length =
        MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring result(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(),
                        length);
    return result;
}

auto make_icon(COLORREF color, mode_state state) -> unique_icon {
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = 32;
    info.bmiHeader.biHeight = -32;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* pixels{};
    unique_gdi<HBITMAP> bitmap{
        CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &pixels, nullptr, 0)};
    unique_gdi<HBITMAP> mask{CreateBitmap(32, 32, 1, 1, nullptr)};
    if (!bitmap || !mask) {
        return {};
    }
    auto* data = static_cast<DWORD*>(pixels);
    for (int y = 0; y < 32; ++y) {
        for (int x = 0; x < 32; ++x) {
            const auto dx = x - 15.5, dy = y - 15.5;
            const auto radius = std::sqrt(dx * dx + dy * dy);
            const bool symbol =
                state == mode_state::attention
                    ? (x >= 14 && x <= 17 && ((y >= 7 && y <= 18) || (y >= 22 && y <= 25)))
                    : ((radius >= 7 && radius <= 9 && !(y < 14 && x > 11 && x < 20)) ||
                       (x >= 14 && x <= 17 && y >= 5 && y <= 15));
            const auto ink = symbol ? background : color;
            data[y * 32 + x] = radius <= 15
                                   ? 0xff000000U | (static_cast<DWORD>(GetRValue(ink)) << 16U) |
                                         (static_cast<DWORD>(GetGValue(ink)) << 8U) | GetBValue(ink)
                                   : 0;
        }
    }
    ICONINFO descriptor{};
    descriptor.fIcon = TRUE;
    descriptor.hbmColor = bitmap.get();
    descriptor.hbmMask = mask.get();
    return unique_icon{CreateIconIndirect(&descriptor)};
}

class window_class final {
  public:
    explicit window_class(HINSTANCE instance) noexcept : instance_{instance} {}
    ~window_class() {
        if (registered_) {
            UnregisterClassW(window_class_name, instance_);
        }
    }
    window_class(const window_class&) = delete;
    auto operator=(const window_class&) -> window_class& = delete;
    auto register_class(WNDPROC procedure) noexcept -> std::expected<void, std::error_code> {
        WNDCLASSEXW descriptor{};
        descriptor.cbSize = sizeof(descriptor);
        descriptor.lpfnWndProc = procedure;
        descriptor.hInstance = instance_;
        descriptor.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        descriptor.lpszClassName = window_class_name;
        registered_ = RegisterClassExW(&descriptor) != 0;
        if (!registered_) {
            return std::unexpected(last_error());
        }
        return {};
    }

  private:
    HINSTANCE instance_{};
    bool registered_{};
};

class main_window final {
  public:
    explicit main_window(gaming_mode* mode) : mode_{mode} {}
    ~main_window() {
        if (mode_ && mode_->needs_restore()) {
            try {
                mode_->disable();
            } catch (...) { /* Durable recovery remains for next launch. */
            }
        }
        remove_tray();
        if (power_notification_) {
            UnregisterPowerSettingNotification(power_notification_);
        }
        if (handle_) {
            DestroyWindow(handle_);
        }
    }
    main_window(const main_window&) = delete;
    auto operator=(const main_window&) -> main_window& = delete;

    auto create(HINSTANCE instance, int show_command) -> std::expected<void, std::error_code> {
        background_brush_.reset(CreateSolidBrush(background));
        surface_brush_.reset(CreateSolidBrush(surface));
        off_icon_ = make_icon(muted, mode_state::off);
        on_icon_ = make_icon(accent, mode_state::on);
        warning_icon_ = make_icon(warning, mode_state::attention);
        if (!background_brush_ || !surface_brush_ || !off_icon_ || !on_icon_ || !warning_icon_) {
            return std::unexpected(last_error());
        }
        dpi_ = GetDpiForSystem();
        RECT bounds{0, 0, scale(680), scale(490)};
        AdjustWindowRectExForDpi(&bounds, WS_OVERLAPPEDWINDOW, FALSE, 0, dpi_);
        const auto window = CreateWindowExW(
            0, window_class_name, L"GamePilot", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
            CW_USEDEFAULT, CW_USEDEFAULT, bounds.right - bounds.left, bounds.bottom - bounds.top,
            nullptr, nullptr, instance, this);
        if (!window) {
            return std::unexpected(last_error());
        }
        const BOOL dark = TRUE;
        DwmSetWindowAttribute(handle_, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
        DwmSetWindowAttribute(handle_, DWMWA_CAPTION_COLOR, &background, sizeof(background));
        DwmSetWindowAttribute(handle_, DWMWA_TEXT_COLOR, &foreground, sizeof(foreground));
        const std::array labels{L"GamePilot",
                                L"Your gaming controls, in one place.",
                                L"Gaming mode",
                                L"OFF",
                                L"Your normal lid settings are in effect.",
                                L"Lid close behavior",
                                L"Battery + plugged in",
                                L"Idle sleep and critical-battery actions still apply.",
                                L"Close the window to keep GamePilot in the tray."};
        for (std::size_t i = 0; i < labels.size(); ++i) {
            labels_[i] = CreateWindowExW(0, L"STATIC", labels[i], WS_CHILD | WS_VISIBLE | SS_LEFT,
                                         0, 0, 0, 0, handle_, nullptr, instance, nullptr);
            if (!labels_[i]) {
                return std::unexpected(last_error());
            }
        }
        toggle_ = CreateWindowExW(0, L"BUTTON", L"Turn on gaming mode",
                                  WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 0, 0,
                                  handle_, reinterpret_cast<HMENU>(toggle_id), instance, nullptr);
        exit_ = CreateWindowExW(0, L"BUTTON", L"Exit and restore",
                                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 0, 0,
                                handle_, reinterpret_cast<HMENU>(exit_id), instance, nullptr);
        if (!toggle_ || !exit_) {
            return std::unexpected(last_error());
        }
        update_fonts(GetDpiForWindow(handle_));
        if (mode_ && mode_->needs_restore()) {
            try {
                mode_->disable();
            } catch (const std::exception&) { /* Display recovery failure below. */
            }
        }
        refresh();
        if (mode_) {
            taskbar_created_ = RegisterWindowMessageW(L"TaskbarCreated");
            add_tray();
            power_notification_ = RegisterPowerSettingNotification(
                handle_, &GUID_POWERSCHEME_PERSONALITY, DEVICE_NOTIFY_WINDOW_HANDLE);
        }
        ShowWindow(handle_, show_command);
        UpdateWindow(handle_);
        SetFocus(toggle_);
        return {};
    }

    auto request_exit() const noexcept -> std::expected<void, std::error_code> {
        if (!PostMessageW(handle_, WM_COMMAND, exit_id, 0)) {
            return std::unexpected(last_error());
        }
        return {};
    }
    auto handle() const noexcept -> HWND { return handle_; }

    static auto CALLBACK procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) noexcept
        -> LRESULT {
        auto* self = reinterpret_cast<main_window*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            const auto* creation = reinterpret_cast<const CREATESTRUCTW*>(lparam);
            self = static_cast<main_window*>(creation->lpCreateParams);
            self->handle_ = window;
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (!self) {
            return DefWindowProcW(window, message, wparam, lparam);
        }
        // No C++ exception may cross the Windows callback boundary.
        try {
            return self->dispatch(message, wparam, lparam);
        } catch (...) {
            SetWindowTextW(window, L"GamePilot - needs attention");
            ShowWindow(window, SW_SHOW);
            MessageBoxW(window,
                        L"GamePilot encountered an error. Exit and restore to retry recovery. "
                        L"Saved settings are kept for the next launch.",
                        L"GamePilot", MB_OK | MB_ICONERROR);
            return 0;
        }
    }

  private:
    auto scale(int value) const noexcept -> int {
        return MulDiv(value, static_cast<int>(dpi_), 96);
    }
    auto state_color() const noexcept -> COLORREF {
        return status_.state == mode_state::on          ? accent
               : status_.state == mode_state::attention ? warning
                                                        : muted;
    }
    auto state_label() const noexcept -> const wchar_t* {
        return status_.state == mode_state::on          ? L"ON"
               : status_.state == mode_state::attention ? L"NEEDS ATTENTION"
                                                        : L"OFF";
    }
    auto current_icon() const noexcept -> HICON {
        return status_.state == mode_state::on          ? on_icon_.get()
               : status_.state == mode_state::attention ? warning_icon_.get()
                                                        : off_icon_.get();
    }
    void update_fonts(UINT dpi) {
        dpi_ = dpi;
        const std::array sizes{28, 14, 22};
        for (std::size_t i = 0; i < sizes.size(); ++i) {
            unique_gdi<HFONT> font{
                CreateFontW(-scale(sizes[i]), 0, 0, 0, i == 1 ? FW_NORMAL : FW_SEMIBOLD, FALSE,
                            FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI")};
            if (!font) {
                throw std::system_error(last_error(), "Create interface font");
            }
            fonts_[i] = std::move(font);
        }
        for (std::size_t i = 0; i < labels_.size(); ++i) {
            const auto index = i == 0 ? 0U : i == 2 || i == 5 ? 2U : 1U;
            SendMessageW(labels_[i], WM_SETFONT, reinterpret_cast<WPARAM>(fonts_[index].get()),
                         TRUE);
        }
        SendMessageW(toggle_, WM_SETFONT, reinterpret_cast<WPARAM>(fonts_[1].get()), TRUE);
        SendMessageW(exit_, WM_SETFONT, reinterpret_cast<WPARAM>(fonts_[1].get()), TRUE);
        layout();
    }
    void layout() noexcept {
        if (!toggle_) {
            return;
        }
        RECT client{};
        GetClientRect(handle_, &client);
        const int width = MulDiv(client.right, 96, static_cast<int>(dpi_));
        const auto place = [this](HWND child, int x, int y, int w, int h) {
            MoveWindow(child, scale(x), scale(y), scale(w), scale(h), TRUE);
        };
        place(labels_[0], 32, 24, width - 64, 38);
        place(labels_[1], 32, 68, width - 64, 24);
        place(labels_[2], 56, 135, 230, 32);
        place(labels_[3], width - 222, 145, 166, 24);
        place(labels_[4], 56, 184, width - 112, 74);
        place(toggle_, 56, 271, 236, 42);
        place(labels_[5], 32, 352, 240, 30);
        place(labels_[6], width - 216, 361, 184, 24);
        place(labels_[7], 32, 392, width - 64, 24);
        place(labels_[8], 32, 435, width - 235, 40);
        place(exit_, width - 190, 433, 158, 40);
        InvalidateRect(handle_, nullptr, TRUE);
    }
    void refresh() {
        const auto next =
            mode_ ? mode_->status()
                  : mode_status{mode_state::off, "Your normal lid settings are in effect."};
        const bool changed = next != status_;
        status_ = next;
        if (mode_ && mode_->needs_restore()) {
            if (!timer_running_) {
                if (!SetTimer(handle_, verify_timer, 5000, nullptr)) {
                    throw std::system_error(last_error(), "Start status verification");
                }
                timer_running_ = true;
            }
        } else if (timer_running_) {
            KillTimer(handle_, verify_timer);
            timer_running_ = false;
        }
        if (!changed && initialized_) {
            return;
        }
        initialized_ = true;
        SetWindowTextW(labels_[3], state_label());
        SetWindowTextW(labels_[4], widen(status_.detail).c_str());
        SetWindowTextW(toggle_, status_.state == mode_state::on   ? L"Turn off gaming mode"
                                : mode_ && mode_->needs_restore() ? L"Restore settings"
                                                                  : L"Turn on gaming mode");
        const auto title = std::wstring{L"GamePilot - Gaming mode "} + state_label();
        SetWindowTextW(handle_, title.c_str());
        SendMessageW(handle_, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(current_icon()));
        SendMessageW(handle_, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(current_icon()));
        update_tray();
        InvalidateRect(handle_, nullptr, TRUE);
        InvalidateRect(toggle_, nullptr, TRUE);
        if (changed && status_.state == mode_state::attention && tray_added_) {
            auto data = tray_data();
            data.uFlags = NIF_INFO;
            lstrcpyW(data.szInfoTitle, L"Gaming mode needs attention");
            lstrcpyW(data.szInfo, L"Open GamePilot to review and restore your lid settings.");
            data.dwInfoFlags = NIIF_WARNING;
            Shell_NotifyIconW(NIM_MODIFY, &data);
        }
    }
    void toggle() {
        if (!mode_) {
            return;
        }
        try {
            if (mode_->needs_restore()) {
                mode_->disable();
            } else {
                mode_->enable();
            }
        } catch (const std::exception&) { /* Controller retains the detailed error for the UI. */
        }
        refresh();
    }
    void exit_safely() {
        if (mode_) {
            try {
                mode_->disable();
            } catch (const std::exception&) {
                refresh();
                show();
                return;
            }
        }
        DestroyWindow(handle_);
    }
    void show() noexcept {
        ShowWindow(handle_, IsIconic(handle_) ? SW_RESTORE : SW_SHOW);
        SetForegroundWindow(handle_);
        SetFocus(toggle_);
    }
    auto tray_data() const noexcept -> NOTIFYICONDATAW {
        NOTIFYICONDATAW data{};
        data.cbSize = sizeof(data);
        data.hWnd = handle_;
        data.uID = 1;
        return data;
    }
    void add_tray() {
        auto data = tray_data();
        data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
        data.uCallbackMessage = tray_message;
        data.hIcon = current_icon();
        const auto title = std::wstring{L"GamePilot: "} + state_label();
        lstrcpynW(data.szTip, title.c_str(), static_cast<int>(std::size(data.szTip)));
        tray_added_ = Shell_NotifyIconW(NIM_ADD, &data) != FALSE;
        if (tray_added_) {
            data.uVersion = NOTIFYICON_VERSION_4;
            Shell_NotifyIconW(NIM_SETVERSION, &data);
            SetWindowTextW(labels_[8], L"Close the window to keep GamePilot in the tray.");
        } else {
            SetWindowTextW(labels_[8], L"Tray unavailable. Closing exits and restores settings.");
            show();
        }
    }
    void update_tray() {
        if (!tray_added_) {
            return;
        }
        auto data = tray_data();
        data.uFlags = NIF_ICON | NIF_TIP | NIF_SHOWTIP;
        data.hIcon = current_icon();
        const auto title = std::wstring{L"GamePilot: "} + state_label();
        lstrcpynW(data.szTip, title.c_str(), static_cast<int>(std::size(data.szTip)));
        Shell_NotifyIconW(NIM_MODIFY, &data);
    }
    void remove_tray() noexcept {
        if (tray_added_) {
            auto data = tray_data();
            Shell_NotifyIconW(NIM_DELETE, &data);
            tray_added_ = false;
        }
    }
    void tray_menu() {
        refresh();
        const auto menu = CreatePopupMenu();
        if (!menu) {
            show();
            return;
        }
        const auto heading = std::wstring{L"Gaming mode: "} + state_label();
        AppendMenuW(menu, MF_STRING | MF_DISABLED, 0, heading.c_str());
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, open_id, L"Open GamePilot");
        AppendMenuW(menu, MF_STRING, toggle_id,
                    mode_ && mode_->needs_restore() ? L"Turn off / restore settings"
                                                    : L"Turn on gaming mode");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, exit_id, L"Exit and restore");
        POINT point{};
        GetCursorPos(&point);
        SetForegroundWindow(handle_);
        const auto command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y,
                                            0, handle_, nullptr);
        DestroyMenu(menu);
        PostMessageW(handle_, WM_NULL, 0, 0);
        if (command) {
            PostMessageW(handle_, WM_COMMAND, static_cast<WPARAM>(command), 0);
        }
    }
    void paint() noexcept {
        PAINTSTRUCT paint{};
        const auto context = BeginPaint(handle_, &paint);
        RECT bounds{};
        GetClientRect(handle_, &bounds);
        FillRect(context, &bounds, background_brush_.get());
        const auto old_pen = SelectObject(context, GetStockObject(NULL_PEN));
        const auto old_brush = SelectObject(context, surface_brush_.get());
        RoundRect(context, scale(32), scale(112), bounds.right - scale(32), scale(337), scale(16),
                  scale(16));
        SelectObject(context, old_brush);
        SelectObject(context, old_pen);
        EndPaint(handle_, &paint);
    }
    void draw_button(const DRAWITEMSTRUCT& item) noexcept {
        const bool primary = item.CtlID == toggle_id;
        auto bounds = item.rcItem;
        const bool pressed = (item.itemState & ODS_SELECTED) != 0;
        const auto fill = primary ? (pressed ? RGB(68, 180, 133) : accent)
                                  : (pressed ? RGB(47, 57, 72) : surface);
        SetDCBrushColor(item.hDC, fill);
        FillRect(item.hDC, &bounds, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
        const auto old_font = SelectObject(item.hDC, fonts_[1].get());
        SetBkMode(item.hDC, TRANSPARENT);
        SetTextColor(item.hDC, primary ? background : foreground);
        std::array<wchar_t, 80> text{};
        GetWindowTextW(item.hwndItem, text.data(), static_cast<int>(text.size()));
        DrawTextW(item.hDC, text.data(), -1, &bounds, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        if ((item.itemState & ODS_FOCUS) != 0) {
            InflateRect(&bounds, -scale(4), -scale(4));
            DrawFocusRect(item.hDC, &bounds);
        }
        SelectObject(item.hDC, old_font);
    }
    auto dispatch(UINT message, WPARAM wparam, LPARAM lparam) -> LRESULT {
        if (taskbar_created_ != 0 && message == taskbar_created_) {
            tray_added_ = false;
            add_tray();
            return 0;
        }
        switch (message) {
        case WM_PAINT:
            paint();
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_SIZE:
            layout();
            return 0;
        case WM_GETMINMAXINFO: {
            auto* limits = reinterpret_cast<MINMAXINFO*>(lparam);
            RECT minimum{0, 0, scale(640), scale(490)};
            AdjustWindowRectExForDpi(&minimum, WS_OVERLAPPEDWINDOW, FALSE, 0, dpi_);
            limits->ptMinTrackSize = {minimum.right - minimum.left, minimum.bottom - minimum.top};
            return 0;
        }
        case WM_DPICHANGED: {
            update_fonts(HIWORD(wparam));
            const auto* bounds = reinterpret_cast<const RECT*>(lparam);
            SetWindowPos(handle_, nullptr, bounds->left, bounds->top, bounds->right - bounds->left,
                         bounds->bottom - bounds->top, SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }
        case WM_CTLCOLORSTATIC: {
            const auto context = reinterpret_cast<HDC>(wparam);
            const auto child = reinterpret_cast<HWND>(lparam);
            const bool card = child == labels_[2] || child == labels_[3] || child == labels_[4];
            SetBkColor(context, card ? surface : background);
            SetTextColor(context,
                         child == labels_[3] ? state_color()
                         : child == labels_[0] || child == labels_[2] || child == labels_[5]
                             ? foreground
                             : muted);
            return reinterpret_cast<LRESULT>(card ? surface_brush_.get() : background_brush_.get());
        }
        case WM_DRAWITEM:
            draw_button(*reinterpret_cast<const DRAWITEMSTRUCT*>(lparam));
            return TRUE;
        case WM_COMMAND:
            switch (LOWORD(wparam)) {
            case toggle_id:
                toggle();
                break;
            case exit_id:
                exit_safely();
                break;
            case open_id:
                show();
                break;
            default:
                break;
            }
            return 0;
        case WM_CLOSE:
            if (tray_added_) {
                ShowWindow(handle_, SW_HIDE);
            } else {
                exit_safely();
            }
            return 0;
        case WM_QUERYENDSESSION:
            return TRUE;
        case WM_ENDSESSION:
            if (wparam != 0 && mode_) {
                try {
                    mode_->disable();
                } catch (...) { /* Recover at next launch after forced shutdown. */
                }
            }
            return 0;
        case WM_POWERBROADCAST:
            PostMessageW(handle_, verify_message, 0, 0);
            return TRUE;
        case WM_SETTINGCHANGE:
            PostMessageW(handle_, verify_message, 0, 0);
            return 0;
        case WM_TIMER:
            if (wparam == verify_timer) {
                refresh();
            }
            return 0;
        case verify_message:
            if (initialized_) {
                refresh();
            }
            return 0;
        case show_message:
            refresh();
            show();
            return 0;
        case tray_message:
            if (LOWORD(lparam) == WM_CONTEXTMENU) {
                tray_menu();
            } else if (LOWORD(lparam) == NIN_SELECT || LOWORD(lparam) == NIN_KEYSELECT ||
                       LOWORD(lparam) == WM_LBUTTONDBLCLK) {
                refresh();
                show();
            }
            return 0;
        case WM_DESTROY:
            remove_tray();
            PostQuitMessage(0);
            return 0;
        case WM_NCDESTROY: {
            const auto window = handle_;
            SetWindowLongPtrW(window, GWLP_USERDATA, 0);
            handle_ = nullptr;
            return DefWindowProcW(window, message, wparam, lparam);
        }
        default:
            return DefWindowProcW(handle_, message, wparam, lparam);
        }
    }

    gaming_mode* mode_{};
    HWND handle_{}, toggle_{}, exit_{};
    std::array<HWND, 9> labels_{};
    std::array<unique_gdi<HFONT>, 3> fonts_{};
    unique_gdi<HBRUSH> background_brush_{}, surface_brush_{};
    unique_icon on_icon_{}, off_icon_{}, warning_icon_{};
    mode_status status_{};
    UINT dpi_{96}, taskbar_created_{};
    HPOWERNOTIFY power_notification_{};
    bool tray_added_{}, timer_running_{}, initialized_{};
};

} // namespace

auto run_application(HINSTANCE instance, int show_command, bool smoke_test)
    -> std::expected<int, std::error_code> {
    std::unique_ptr<void, handle_deleter> single_instance;
    if (!smoke_test) {
        single_instance.reset(CreateMutexW(nullptr, FALSE, L"Local\\GamePilot.MainWindow"));
        const auto error = GetLastError();
        if (!single_instance) {
            return std::unexpected(last_error());
        }
        if (error == ERROR_ALREADY_EXISTS) {
            if (const auto window = FindWindowW(window_class_name, nullptr)) {
                PostMessageW(window, show_message, 0, 0);
            }
            return 0;
        }
    }
    window_class registration{instance};
    if (const auto result = registration.register_class(main_window::procedure); !result) {
        return std::unexpected(result.error());
    }
    windows_power_settings power;
    std::optional<file_recovery_store> store;
    std::optional<gaming_mode> mode;
    // Smoke tests never access real recovery data or machine power settings.
    if (!smoke_test) {
        store.emplace(recovery_path());
        mode.emplace(power, *store);
    }
    main_window window{mode ? &*mode : nullptr};
    if (const auto result = window.create(instance, smoke_test ? SW_HIDE : show_command); !result) {
        return std::unexpected(result.error());
    }
    if (smoke_test) {
        if (const auto result = window.request_exit(); !result) {
            return std::unexpected(result.error());
        }
    }
    MSG message{};
    for (;;) {
        const auto result = GetMessageW(&message, nullptr, 0, 0);
        if (result == -1) {
            return std::unexpected(last_error());
        }
        if (result == 0) {
            return static_cast<int>(message.wParam);
        }
        if (!IsDialogMessageW(window.handle(), &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
}

} // namespace gamepilot::win32
