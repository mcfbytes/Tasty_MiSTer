// SPDX-License-Identifier: GPL-3.0-or-later
#include "os/uinput_keyboard.h"

#include <fcntl.h>
#include <linux/uinput.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

namespace mister::os {

namespace {

bool emit(int fd, std::uint16_t type, std::uint16_t code, std::int32_t value) noexcept {
    input_event ev{};
    ev.type = type;
    ev.code = code;
    ev.value = value;
    ssize_t n = 0;
    do {
        n = ::write(fd, &ev, sizeof ev);
    } while (n < 0 && errno == EINTR);
    return n == static_cast<ssize_t>(sizeof ev);
}

}  // namespace

Ex<void> UinputKeyboard::open() noexcept {
    TASTY_SEAT_BODY(UinputKeyboard);
    if (fd_.valid()) return {};
    UniqueFd fd{::open("/dev/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC)};
    if (!fd.valid())
        return std::unexpected(Error{Errc::os, ERR_SITE(), static_cast<std::uint32_t>(errno)});
    if (::ioctl(fd.get(), UI_SET_EVBIT, EV_KEY) != 0 ||
        ::ioctl(fd.get(), UI_SET_EVBIT, EV_SYN) != 0)
        return std::unexpected(Error{Errc::os, ERR_SITE(), static_cast<std::uint32_t>(errno)});
    for (int code = 1; code < 256; ++code)
        (void)::ioctl(fd.get(), UI_SET_KEYBIT, code);
    uinput_setup setup{};
    setup.id.bustype = BUS_VIRTUAL;
    std::strncpy(setup.name, kName, UINPUT_MAX_NAME_SIZE - 1);
    if (::ioctl(fd.get(), UI_DEV_SETUP, &setup) != 0 || ::ioctl(fd.get(), UI_DEV_CREATE) != 0)
        return std::unexpected(Error{Errc::os, ERR_SITE(), static_cast<std::uint32_t>(errno)});
    fd_ = std::move(fd);
    return {};
}

void UinputKeyboard::key(std::uint16_t code, bool down) noexcept {
    TASTY_SEAT_BODY(UinputKeyboard);
    if (!fd_.valid()) return;
    if (emit(fd_.get(), EV_KEY, code, down ? 1 : 0)) (void)emit(fd_.get(), EV_SYN, SYN_REPORT, 0);
}

}  // namespace mister::os
