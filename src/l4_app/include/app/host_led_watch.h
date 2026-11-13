// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string_view>

#include "infra/error.h"
#include "infra/seat.h"
#include "infra/unique_fd.h"
#include "infra/wake_flag.h"

namespace mister::app {

class HostLedWatch {
    TASTY_SEAT_RESIDENT(Input);

public:
    static constexpr std::string_view kSysfsLeds = "/sys/class/leds/";
    static constexpr std::string_view kAttribute = "/brightness_hw_changed";

    [[nodiscard]] static Ex<HostLedWatch> open(
        std::string_view node,
        xthread::WakeFlag& activity
    ) noexcept;

    [[nodiscard]] static Ex<HostLedWatch> open_at(
        std::string_view prefix,
        std::string_view node,
        xthread::WakeFlag& activity
    ) noexcept;

    HostLedWatch(HostLedWatch&&) noexcept = default;
    HostLedWatch& operator=(HostLedWatch&&) noexcept = default;
    HostLedWatch(const HostLedWatch&) = delete;
    HostLedWatch& operator=(const HostLedWatch&) = delete;

    [[nodiscard]] const UniqueFd& fd() const noexcept { return fd_; }

    [[nodiscard]] static const UniqueFd& fd_of(const HostLedWatch& watch) noexcept {
        return watch.fd_;
    }

    void on_ready() noexcept;

private:
    HostLedWatch(UniqueFd fd, xthread::WakeFlag& activity) noexcept;

    UniqueFd fd_;
    xthread::WakeFlag* activity_;
};

}
