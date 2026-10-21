// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string_view>

#include "infra/error.h"
#include "infra/seat.h"
#include "infra/unique_fd.h"

namespace mister::svc {

class HotplugWatch {
public:
    TASTY_SEAT_RESIDENT(Input);

    [[nodiscard]] Ex<void> open(std::string_view dir);

    [[nodiscard]] Ex<void> on_ready();

    bool pending() const noexcept { return pending_; }

    bool take_pending() noexcept {
        const bool p = pending_;
        pending_ = false;
        return p;
    }

    bool valid() const noexcept { return inotify_.valid(); }

    int fd() const noexcept { return inotify_.get(); }

private:
    UniqueFd inotify_{};
    bool pending_ = false;
};

}
