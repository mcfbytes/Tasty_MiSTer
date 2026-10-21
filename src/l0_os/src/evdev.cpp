// SPDX-License-Identifier: GPL-3.0-or-later
#include "os/evdev.h"

#include <sys/ioctl.h>
#include <unistd.h>

namespace mister::os {

ssize_t Evdev::read(std::span<input_event> out) const noexcept {
    return ::read(fd_.get(), out.data(), out.size_bytes());
}

std::optional<input_absinfo> Evdev::abs_info(unsigned code) const noexcept {
    input_absinfo info{};
    if (::ioctl(fd_.get(), EVIOCGABS(code), &info) < 0)
        return std::nullopt;
    return info;
}

std::optional<input_id> Evdev::id() const noexcept {
    input_id info{};
    if (::ioctl(fd_.get(), EVIOCGID, &info) < 0)
        return std::nullopt;
    return info;
}

std::optional<std::size_t> Evdev::name(std::span<char> out) const noexcept {
    const int n = ::ioctl(fd_.get(), EVIOCGNAME(out.size()), out.data());
    if (n < 0)
        return std::nullopt;
    return static_cast<std::size_t>(n);
}

std::optional<std::size_t> Evdev::uniq(std::span<char> out) const noexcept {
    const int n = ::ioctl(fd_.get(), EVIOCGUNIQ(out.size()), out.data());
    if (n < 0)
        return std::nullopt;
    return static_cast<std::size_t>(n);
}

std::optional<int> Evdev::effect_count() const noexcept {
    int effects = 0;
    if (::ioctl(fd_.get(), EVIOCGEFFECTS, &effects) < 0)
        return std::nullopt;
    return effects;
}

bool Evdev::has_ff(unsigned code) const noexcept {
    unsigned char ff[(FF_MAX + 7) / 8] = {};
    if (::ioctl(fd_.get(), EVIOCGBIT(EV_FF, sizeof ff), ff) < 0)
        return false;
    return (ff[code / 8u] & (1u << (code % 8u))) != 0u;
}

bool Evdev::grab(bool on) const noexcept {
    return ::ioctl(fd_.get(), EVIOCGRAB, on ? 1 : 0) >= 0;
}

bool Evdev::erase_effect(int id) const noexcept {
    return ::ioctl(fd_.get(), EVIOCRMFF, id) >= 0;
}

bool Evdev::upload_effect(ff_effect& e) const noexcept {
    return ::ioctl(fd_.get(), EVIOCSFF, &e) >= 0;
}

}
