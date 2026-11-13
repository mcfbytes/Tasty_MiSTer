// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/seat.h"
#include "os/epoll_set.h"
#include "svc/connect_fd_delta.h"
#include "svc/types.h"

namespace mister::app {

class ConnectFdSync {
    TASTY_SEAT_RESIDENT(Connect);

public:
    explicit ConnectFdSync(const os::EpollSet& set) noexcept : set_(set) {}

    template <class Service>
    void apply(const svc::ConnectFdDelta& delta, Service& service) noexcept {
        for (const svc::ConnectFdDelta::Fd fd : delta.removed()) {
            if (fd.open())
                fd.remove_from(set_);
        }
        if (!delta.removed().empty())
            service.fds_removed();
        for (const svc::ConnectFdDelta::Fd fd : delta.added()) {
            if (fd.open() && !fd.add_to(set_))
                add_failures_.add();
        }
    }

    [[nodiscard]] svc::Tally add_failures() const noexcept { return add_failures_; }

private:
    const os::EpollSet& set_;
    svc::Tally add_failures_{};
};

}
