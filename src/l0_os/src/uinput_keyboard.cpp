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

constexpr std::uint16_t kKeyCodeCount = 256;

bool emit(int fd, const input_event& ev) noexcept {
    ssize_t n = 0;
    do {
        n = ::write(fd, &ev, sizeof ev);
    } while (n < 0 && errno == EINTR);
    return n == static_cast<ssize_t>(sizeof ev);
}

}

Ex<void> UinputKeyboard::open() noexcept {
    TASTY_SEAT_BODY(UinputKeyboard);
    if (fd_.valid())
        return {};
    UniqueFd fd{::open("/dev/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC)};
    if (!fd.valid())
        return std::unexpected(infra::errno_error(Errc::os, ERR_SITE(), errno));
    if (::ioctl(fd.get(), UI_SET_EVBIT, EV_KEY) != 0
        || ::ioctl(fd.get(), UI_SET_EVBIT, EV_SYN) != 0)
        return std::unexpected(infra::errno_error(Errc::os, ERR_SITE(), errno));
    for (std::uint16_t code = 1; code < kKeyCodeCount; ++code)
        (void)::ioctl(fd.get(), UI_SET_KEYBIT, code);
    uinput_setup setup{};
    setup.id.bustype = BUS_VIRTUAL;
    std::strncpy(setup.name, kName, UINPUT_MAX_NAME_SIZE - 1);
    if (::ioctl(fd.get(), UI_DEV_SETUP, &setup) != 0 || ::ioctl(fd.get(), UI_DEV_CREATE) != 0)
        return std::unexpected(infra::errno_error(Errc::os, ERR_SITE(), errno));
    fd_ = std::move(fd);
    return {};
}

void UinputKeyboard::key(std::uint16_t code, bool down) noexcept {
    TASTY_SEAT_BODY(UinputKeyboard);
    if (!fd_.valid())
        return;
    input_event key_event{};
    key_event.type = EV_KEY;
    key_event.code = code;
    key_event.value = down ? 1 : 0;
    if (emit(fd_.get(), key_event)) {
        input_event sync_event{};
        sync_event.type = EV_SYN;
        sync_event.code = SYN_REPORT;
        sync_event.value = 0;
        (void)emit(fd_.get(), sync_event);
    }
}

}
