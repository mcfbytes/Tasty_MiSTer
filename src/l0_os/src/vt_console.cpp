// SPDX-License-Identifier: GPL-3.0-or-later
#include "os/linux_vt_console.h"

#include <fcntl.h>
#include <linux/kd.h>
#include <linux/vt.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <span>
#include <string_view>

#include "infra/unique_fd.h"
#include "os/path_buf.h"
#include "os/ui_vt_console.h"

namespace mister::os {
namespace {

constexpr const char* kConsole = "/dev/tty0";

UniqueFd open_rw(const char* path) noexcept {
    return UniqueFd{::open(path, O_RDWR | O_NOCTTY | O_CLOEXEC | O_NONBLOCK)};
}

[[nodiscard]] constexpr int vt_arg(VtNumber vt) noexcept {
    return static_cast<int>(vt.v);
}

UniqueFd open_vt(VtNumber vt) noexcept {
    char path[kVtTtyPathBuf];
    if (!format_path(std::span<char>(path), "/dev/tty%d", vt_arg(vt)))
        return {};
    return open_rw(path);
}

void write_all(int fd, std::string_view s) noexcept {
    while (!s.empty()) {
        const ssize_t n = ::write(fd, s.data(), s.size());
        if (n <= 0)
            return;
        s.remove_prefix(static_cast<std::size_t>(n));
    }
}

std::optional<VtNumber> active_op() noexcept {
    const UniqueFd fd = open_rw(kConsole);
    if (!fd.valid())
        return std::nullopt;
    vt_stat st{};
    if (::ioctl(fd.get(), VT_GETSTATE, &st) != 0)
        return std::nullopt;
    return VtNumber{st.v_active};
}

[[nodiscard]] Ex<void> activate_op(VtNumber vt) noexcept {
    const UniqueFd fd = open_rw(kConsole);
    if (!fd.valid())
        return std::unexpected(infra::errno_error(Errc::os, ERR_SITE(), errno));
    if (::ioctl(fd.get(), VT_ACTIVATE, vt_arg(vt)) != 0)
        return std::unexpected(infra::errno_error(Errc::os, ERR_SITE(), errno));
    return {};
}

void blank_op(const char* tty) noexcept {
    const UniqueFd fd = open_rw(tty);
    if (!fd.valid())
        return;
    constexpr std::string_view kBlank = "\033[0m\033[?25l\033[37m\033[40m\033[2J\033[H";
    write_all(fd.get(), kBlank);
}

void restore_text_op(const char* tty) noexcept {
    const UniqueFd fd = open_rw(tty);
    if (!fd.valid())
        return;
    (void)::ioctl(fd.get(), KDSETMODE, KD_TEXT);
    (void)::ioctl(fd.get(), KDSKBMODE, K_XLATE);
    vt_mode mode{};
    mode.mode = VT_AUTO;
    (void)::ioctl(fd.get(), VT_SETMODE, &mode);
    termios t{};
    if (::tcgetattr(fd.get(), &t) == 0) {
        t.c_lflag |= ICANON | ECHO | ISIG | IEXTEN;
        t.c_oflag |= OPOST | ONLCR;
        (void)::tcsetattr(fd.get(), TCSANOW, &t);
    }
    constexpr std::string_view kShow = "\033[0m\033[2J\033[H\033[?25h";
    write_all(fd.get(), kShow);
}

std::optional<bool> text_mode_op(VtNumber vt) noexcept {
    const UniqueFd fd = open_vt(vt);
    if (!fd.valid())
        return std::nullopt;
    int mode = 0;
    if (::ioctl(fd.get(), KDGETMODE, &mode) != 0)
        return std::nullopt;
    return mode == KD_TEXT;
}

void graphics_op(VtNumber vt) noexcept {
    const UniqueFd fd = open_vt(vt);
    if (!fd.valid())
        return;
    constexpr std::string_view kHide = "\033[0m\033[?25l";
    write_all(fd.get(), kHide);
    (void)::ioctl(fd.get(), KDSETMODE, KD_GRAPHICS);
}

void text_op(VtNumber vt) noexcept {
    const UniqueFd fd = open_vt(vt);
    if (!fd.valid())
        return;
    (void)::ioctl(fd.get(), KDSETMODE, KD_TEXT);
}

void cursor_op(VtNumber vt, bool shown) noexcept {
    const UniqueFd fd = open_vt(vt);
    if (!fd.valid())
        return;
    write_all(fd.get(), shown ? std::string_view{"\033[?25h"} : std::string_view{"\033[?25l"});
}

}

std::optional<VtNumber> LinuxVtConsole::active() noexcept {
    TASTY_SEAT_BODY(LinuxVtConsole);
    return active_op();
}

Ex<void> LinuxVtConsole::activate(VtNumber vt) noexcept {
    TASTY_SEAT_BODY(LinuxVtConsole);
    return activate_op(vt);
}

void LinuxVtConsole::blank(const char* tty) noexcept {
    TASTY_SEAT_BODY(LinuxVtConsole);
    blank_op(tty);
}

void LinuxVtConsole::restore_text(const char* tty) noexcept {
    TASTY_SEAT_BODY(LinuxVtConsole);
    restore_text_op(tty);
}

std::optional<bool> LinuxVtConsole::text_mode(VtNumber vt) noexcept {
    TASTY_SEAT_BODY(LinuxVtConsole);
    return text_mode_op(vt);
}

void LinuxVtConsole::graphics(VtNumber vt) noexcept {
    TASTY_SEAT_BODY(LinuxVtConsole);
    graphics_op(vt);
}

void LinuxVtConsole::text(VtNumber vt) noexcept {
    TASTY_SEAT_BODY(LinuxVtConsole);
    text_op(vt);
}

void LinuxVtConsole::cursor(VtNumber vt, bool shown) noexcept {
    TASTY_SEAT_BODY(LinuxVtConsole);
    cursor_op(vt, shown);
}

std::optional<VtNumber> UiVtConsole::active() noexcept {
    TASTY_SEAT_BODY(UiVtConsole);
    return active_op();
}

Ex<void> UiVtConsole::activate(VtNumber vt) noexcept {
    TASTY_SEAT_BODY(UiVtConsole);
    return activate_op(vt);
}

void UiVtConsole::blank(const char* tty) noexcept {
    TASTY_SEAT_BODY(UiVtConsole);
    blank_op(tty);
}

void UiVtConsole::restore_text(const char* tty) noexcept {
    TASTY_SEAT_BODY(UiVtConsole);
    restore_text_op(tty);
}

std::optional<bool> UiVtConsole::text_mode(VtNumber vt) noexcept {
    TASTY_SEAT_BODY(UiVtConsole);
    return text_mode_op(vt);
}

void UiVtConsole::graphics(VtNumber vt) noexcept {
    TASTY_SEAT_BODY(UiVtConsole);
    graphics_op(vt);
}

void UiVtConsole::text(VtNumber vt) noexcept {
    TASTY_SEAT_BODY(UiVtConsole);
    text_op(vt);
}

void UiVtConsole::cursor(VtNumber vt, bool shown) noexcept {
    TASTY_SEAT_BODY(UiVtConsole);
    cursor_op(vt, shown);
}

}
