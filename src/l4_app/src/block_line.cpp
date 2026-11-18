// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/block_line.h"

#include <fcntl.h>
#include <poll.h>

namespace mister::app {

template <class Owner>
bool BlockLine<Owner>::open(
    os::UioLineRef from,
    infra::OptRef<const os::EpollSet> doorbells
) noexcept {
    TASTY_SEAT_BODY(SeatOwner);
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

template <class Owner>
void BlockLine<Owner>::close() noexcept {
    if (line_.fd() >= 0 && doorbells_)
        doorbells_->remove(line_.fd());
    line_ = os::UioHandle{};
    doorbells_ = {};
}

template <class Owner>
bool BlockLine<Owner>::rang() const noexcept {
    TASTY_SEAT_BODY(SeatOwner);
    if (line_.fd() < 0)
        return false;
    pollfd p{line_.fd(), POLLIN, 0};
    return ::poll(&p, 1, 0) > 0 && (p.revents & POLLIN) != 0;
}

template class BlockLine<seat_owner::Io>;

}
