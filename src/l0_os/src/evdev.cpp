// SPDX-License-Identifier: GPL-3.0-or-later
#include "os/evdev.h"

#include <sys/ioctl.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>

namespace mister::os {

int FfEffectId::erase_on(int fd) const noexcept {
    return ::ioctl(fd, EVIOCRMFF, v);
}

Ex<std::size_t> Evdev::read(std::span<input_event> out) const noexcept {
    const ssize_t n = ::read(fd_.get(), out.data(), out.size_bytes());
    if (n < 0)
        return std::unexpected(infra::errno_error(Errc::os, ERR_SITE(), errno));
    return static_cast<std::size_t>(n);
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

std::optional<std::string_view> Evdev::name(std::span<char> out) const noexcept {
    const int n = ::ioctl(fd_.get(), EVIOCGNAME(out.size()), out.data());
    if (n < 0)
        return std::nullopt;
    const std::size_t len = std::min<std::size_t>(
        static_cast<std::size_t>(n),
        out.empty() ? 0 : out.size() - 1
    );
    return infra::text_until_nul(out.first(len));
}

std::optional<std::string_view> Evdev::uniq(std::span<char> out) const noexcept {
    const int n = ::ioctl(fd_.get(), EVIOCGUNIQ(out.size()), out.data());
    if (n < 0)
        return std::nullopt;
    const std::size_t len = std::min<std::size_t>(
        static_cast<std::size_t>(n),
        out.empty() ? 0 : out.size() - 1
    );
    return infra::text_until_nul(out.first(len));
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

Ex<void> Evdev::grab(bool on) const noexcept {
    if (::ioctl(fd_.get(), EVIOCGRAB, on ? 1 : 0) < 0) {
        return std::unexpected(infra::errno_error(Errc::os, ERR_SITE(), errno));
    }
    return {};
}

Ex<void> Evdev::erase_effect(FfEffectId id) const noexcept {
    if (id.erase_on(fd_.get()) < 0) {
        return std::unexpected(infra::errno_error(Errc::os, ERR_SITE(), errno));
    }
    return {};
}

Ex<FfEffectId> Evdev::upload_effect(const ff_effect& e) const noexcept {
    ff_effect uploaded = e;
    if (::ioctl(fd_.get(), EVIOCSFF, &uploaded) < 0) {
        return std::unexpected(infra::errno_error(Errc::os, ERR_SITE(), errno));
    }
    return FfEffectId::from_kernel(uploaded.id);
}

Ex<void> Evdev::play(FfEffectId id) const noexcept {
    const input_event play = id.play_event();
    if (::write(fd_.get(), &play, sizeof play) < 0) {
        return std::unexpected(infra::errno_error(Errc::os, ERR_SITE(), errno));
    }
    return {};
}

}
