// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string_view>
#include <utility>

#include "infra/error.h"
#include "infra/seat.h"
#include "infra/unique_fd.h"

namespace mister::svc {

class HotplugWatch {
public:
    TASTY_SEAT_RESIDENT(Input);

    [[nodiscard]] static Ex<HotplugWatch> open(std::string_view dir);

    [[nodiscard]] Ex<void> on_ready();

    bool pending() const noexcept { return pending_; }

    bool take_pending() noexcept {
        const bool p = pending_;
        pending_ = false;
        return p;
    }

    int fd() const noexcept { return inotify_.get(); }

private:
    explicit HotplugWatch(UniqueFd inotify) noexcept : inotify_(std::move(inotify)) {}

    UniqueFd inotify_;
    bool pending_ = false;
};

}
