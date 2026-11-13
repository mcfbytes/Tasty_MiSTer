// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <sys/epoll.h>

#include <array>
#include <optional>
#include <ranges>

#include "infra/error.h"
#include "infra/park_timeout.h"
#include "infra/seat.h"
#include "infra/unique_fd.h"

namespace mister::os {

class EpollSet {
    TASTY_SEAT_EXEMPT(const_shared);

public:
    static constexpr int kMaxReady = 16;

    class Event {
    public:
        [[nodiscard]] int fd() const noexcept { return ev_.data.fd; }

        [[nodiscard]] bool failed() const noexcept {
            return (ev_.events & (EPOLLHUP | EPOLLERR)) != 0u;
        }

    private:
        friend class EpollSet;

        explicit Event(const epoll_event& ev) noexcept : ev_(ev) {}

        epoll_event ev_;
    };

    class Ready {
    public:
        Ready() noexcept = default;

        [[nodiscard]] bool empty() const noexcept { return !slots_[0].has_value(); }

        [[nodiscard]] auto events() const& noexcept {
            return slots_
                | std::views::take_while([](const std::optional<Event>& s) {
                       return s.has_value();
                   })
                | std::views::transform([](const std::optional<Event>& s) { return *s; });
        }

        auto events() const&& = delete;

    private:
        friend class EpollSet;
        std::array<std::optional<Event>, kMaxReady> slots_{};
    };

    [[nodiscard]] static Ex<EpollSet> create() noexcept;
    [[nodiscard]] Ex<void> add(int fd) const noexcept;
    [[nodiscard]] Ex<void> readd(int fd) const noexcept;
    void remove(int fd) const noexcept;
    [[nodiscard]] Ex<Ready> wait(infra::ParkTimeout timeout) const noexcept;

    [[nodiscard]] int fd() const noexcept { return fd_.get(); }

    EpollSet(EpollSet&&) noexcept = default;
    EpollSet(const EpollSet&) = delete;
    EpollSet& operator=(const EpollSet&) = delete;
    EpollSet& operator=(EpollSet&&) = delete;

private:
    explicit EpollSet(UniqueFd fd) noexcept : fd_(std::move(fd)) {}

    UniqueFd fd_;
};

}
