// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <fcntl.h>
#include <poll.h>

#include <cstdint>
#include <utility>

#include "infra/seat.h"
#include "infra/unique_fd.h"
#include "infra/opt_ref.h"
#include "os/epoll_set.h"
#include "os/uio_handle.h"
#include "os/uio_line_ref.h"

namespace mister::app {

class LwSectorEndpoint;
class LwSysEndpoint;

class LwLine {
    TASTY_SEAT_RESIDENT(Io);

public:
    LwLine() = default;

    ~LwLine() { close(); }

    LwLine(const LwLine&) = delete;
    LwLine& operator=(const LwLine&) = delete;

private:
    friend class LwSectorEndpoint;
    friend class LwSysEndpoint;

    [[nodiscard]] bool open(
        os::UioLineRef from,
        infra::OptRef<const os::EpollSet> doorbells
    ) noexcept {
        TASTY_SEAT_BODY(LwLine);
        if (!doorbells)
            return false;
        const int fd = ::fcntl(from.fd(), F_DUPFD_CLOEXEC, 0);
        auto line = os::UioHandle::adopt(from.line(), UniqueFd{fd});
        if (!line)
            return false;
        if (!doorbells->add(line->fd()))
            return false;
        line_ = std::move(*line);
        doorbells_ = doorbells;
        (void)line_.borrow().consume();
        (void)line_.borrow().rearm();
        return true;
    }

    void close() noexcept {
        if (line_.fd() >= 0 && doorbells_)
            doorbells_->remove(line_.fd());
        line_ = os::UioHandle{};
        doorbells_ = {};
    }

    [[nodiscard]] bool rang() const noexcept {
        TASTY_SEAT_BODY(LwLine);
        if (line_.fd() < 0)
            return false;
        pollfd p{line_.fd(), POLLIN, 0};
        return ::poll(&p, 1, 0) > 0 && (p.revents & POLLIN) != 0;
    }

    template <class Port, class Reg>
    [[nodiscard]] std::uint32_t drain(bool rang, Port& port, Reg cause) noexcept {
        TASTY_SEAT_BODY(LwLine);
        if (rang)
            (void)line_.borrow().consume();
        const std::uint32_t w = port.read(cause).bits();
        if (rang)
            (void)line_.borrow().rearm();
        return w;
    }

    os::UioHandle line_{};
    infra::OptRef<const os::EpollSet> doorbells_{};
};

}
