#include "win32/application.hpp"

#include <memory>
#include <type_traits>
#include <utility>

namespace gamepilot::win32 {
namespace {

constexpr auto window_class_name = L"GamePilot.MainWindow";
constexpr auto greeting = L"Hello, world!";

[[nodiscard]] auto last_error() noexcept -> std::error_code {
    return {static_cast<int>(GetLastError()), std::system_category()};
}

struct font_deleter {
    void operator()(HFONT font) const noexcept { DeleteObject(font); }
};
using unique_font = std::unique_ptr<std::remove_pointer_t<HFONT>, font_deleter>;

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

    [[nodiscard]] auto register_class(WNDPROC procedure) noexcept
        -> std::expected<void, std::error_code> {
        const WNDCLASSEXW descriptor{
            .cbSize = sizeof(WNDCLASSEXW),
            .style = 0,
            .lpfnWndProc = procedure,
            .cbClsExtra = 0,
            .cbWndExtra = 0,
            .hInstance = instance_,
            .hIcon = nullptr,
            .hCursor = LoadCursorW(nullptr, IDC_ARROW),
            .hbrBackground = GetSysColorBrush(COLOR_WINDOW),
            .lpszMenuName = nullptr,
            .lpszClassName = window_class_name,
            .hIconSm = nullptr,
        };
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
    main_window() = default;
    ~main_window() {
        if (handle_ != nullptr) {
            DestroyWindow(handle_);
        }
    }
    main_window(const main_window&) = delete;
    auto operator=(const main_window&) -> main_window& = delete;

    [[nodiscard]] auto create(HINSTANCE instance, int show_command) noexcept
        -> std::expected<void, std::error_code> {
        const auto dpi = GetDpiForSystem();
        RECT bounds{0, 0, MulDiv(640, static_cast<int>(dpi), 96),
                    MulDiv(360, static_cast<int>(dpi), 96)};
        if (AdjustWindowRectExForDpi(&bounds, WS_OVERLAPPEDWINDOW, FALSE, 0, dpi) == FALSE) {
            return std::unexpected(last_error());
        }
        const auto handle =
            CreateWindowExW(0, window_class_name, L"GamePilot", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
                            CW_USEDEFAULT, bounds.right - bounds.left, bounds.bottom - bounds.top,
                            nullptr, nullptr, instance, this);
        if (handle == nullptr) {
            return std::unexpected(last_error());
        }
        update_font(GetDpiForWindow(handle));
        ShowWindow(handle, show_command);
        UpdateWindow(handle);
        return {};
    }

    [[nodiscard]] auto request_close() const noexcept -> std::expected<void, std::error_code> {
        if (PostMessageW(handle_, WM_CLOSE, 0, 0) == FALSE) {
            return std::unexpected(last_error());
        }
        return {};
    }

    static auto CALLBACK procedure(HWND handle, UINT message, WPARAM wparam, LPARAM lparam) noexcept
        -> LRESULT {
        auto* self = reinterpret_cast<main_window*>(GetWindowLongPtrW(handle, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            const auto* creation = reinterpret_cast<const CREATESTRUCTW*>(lparam);
            self = static_cast<main_window*>(creation->lpCreateParams);
            self->handle_ = handle;
            SetWindowLongPtrW(handle, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (self == nullptr) {
            return DefWindowProcW(handle, message, wparam, lparam);
        }

        switch (message) {
        case WM_PAINT:
            self->paint();
            return 0;
        case WM_SIZE:
            InvalidateRect(handle, nullptr, TRUE);
            return 0;
        case WM_DPICHANGED: {
            self->update_font(HIWORD(wparam));
            const auto* bounds = reinterpret_cast<const RECT*>(lparam);
            SetWindowPos(handle, nullptr, bounds->left, bounds->top, bounds->right - bounds->left,
                         bounds->bottom - bounds->top, SWP_NOZORDER | SWP_NOACTIVATE);
            InvalidateRect(handle, nullptr, TRUE);
            return 0;
        }
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        case WM_NCDESTROY:
            SetWindowLongPtrW(handle, GWLP_USERDATA, 0);
            self->handle_ = nullptr;
            break;
        default:
            break;
        }
        return DefWindowProcW(handle, message, wparam, lparam);
    }

  private:
    void update_font(UINT dpi) noexcept {
        unique_font next{CreateFontW(-MulDiv(22, static_cast<int>(dpi), 96), 0, 0, 0, FW_NORMAL,
                                     FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                     CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH,
                                     L"Segoe UI")};
        if (next) {
            font_ = std::move(next);
        }
    }

    void paint() const noexcept {
        PAINTSTRUCT paint{};
        const auto context = BeginPaint(handle_, &paint);
        if (context != nullptr) {
            RECT bounds{};
            GetClientRect(handle_, &bounds);
            const auto font = font_ ? font_.get() : GetStockObject(DEFAULT_GUI_FONT);
            const auto previous_font = SelectObject(context, font);
            SetBkMode(context, TRANSPARENT);
            SetTextColor(context, GetSysColor(COLOR_WINDOWTEXT));
            DrawTextW(context, greeting, -1, &bounds, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            SelectObject(context, previous_font);
        }
        EndPaint(handle_, &paint);
    }

    HWND handle_{};
    unique_font font_{};
};

} // namespace

auto run_application(HINSTANCE instance, int show_command, bool smoke_test)
    -> std::expected<int, std::error_code> {
    window_class registration{instance};
    if (const auto result = registration.register_class(main_window::procedure); !result) {
        return std::unexpected(result.error());
    }
    main_window window;
    if (const auto result = window.create(instance, smoke_test ? SW_HIDE : show_command); !result) {
        return std::unexpected(result.error());
    }
    // Exercise real Win32 creation, message dispatch and destruction without showing a test window.
    if (smoke_test) {
        if (const auto result = window.request_close(); !result) {
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
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

} // namespace gamepilot::win32
