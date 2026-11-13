// SPDX-License-Identifier: GPL-3.0-or-later
#include "os/epoll_set.h"

#include <cerrno>
#include <utility>

namespace mister::os {

Ex<EpollSet> EpollSet::create() noexcept {
    UniqueFd fd{::epoll_create1(EPOLL_CLOEXEC)};
    if (!fd.valid())
        return std::unexpected(infra::errno_error(Errc::os, ERR_SITE(), errno));
    return EpollSet{std::move(fd)};
}

Ex<void> EpollSet::add(int fd) const noexcept {
    epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.fd = fd;
    if (::epoll_ctl(fd_.get(), EPOLL_CTL_ADD, fd, &ev) < 0) {
        return std::unexpected(infra::errno_error(Errc::os, ERR_SITE(), errno));
    }
    return {};
}

Ex<void> EpollSet::readd(int fd) const noexcept {
    remove(fd);
    return add(fd);
}

void EpollSet::remove(int fd) const noexcept {
    epoll_event ev{};
    (void)::epoll_ctl(fd_.get(), EPOLL_CTL_DEL, fd, &ev);
}

Ex<EpollSet::Ready> EpollSet::wait(infra::ParkTimeout timeout) const noexcept {
    std::array<epoll_event, kMaxReady> raw{};
    const int n = ::epoll_wait(fd_.get(), raw.data(), kMaxReady, timeout.count());
    if (n < 0)
        return std::unexpected(infra::errno_error(Errc::os, ERR_SITE(), errno));
    Ready ready;
    auto slot = ready.slots_.begin();
    for (const epoll_event& e : raw | std::views::take(n))
        *slot++ = Event{e};
    return ready;
}

}
