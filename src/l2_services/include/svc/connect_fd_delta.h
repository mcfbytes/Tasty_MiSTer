// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "infra/indexed_array.h"
#include "infra/seat.h"
#include "os/epoll_set.h"

namespace mister::svc {

struct ConnectFdDepth {
    std::uint8_t v = 0;

    [[nodiscard]] constexpr std::size_t size() const noexcept {
        const std::size_t places = v;
        return places;
    }

    [[nodiscard]] constexpr ConnectFdDepth next() const noexcept {
        const std::uint8_t place = static_cast<std::uint8_t>(v + 1);
        return ConnectFdDepth{place};
    }

    friend constexpr bool operator==(ConnectFdDepth, ConnectFdDepth) = default;
};

inline constexpr ConnectFdDepth kConnectFdFull{4};

class ConnectFdDelta {
    TASTY_SEAT_EXEMPT(component);

public:
    struct Fd {
        int v = -1;

        [[nodiscard]] constexpr bool open() const noexcept { return v >= 0; }

        [[nodiscard]] bool add_to(const os::EpollSet& set) const noexcept {
            return set.add(v).has_value();
        }

        void remove_from(const os::EpollSet& set) const noexcept { set.remove(v); }

        friend constexpr bool operator==(Fd, Fd) = default;
    };

    void add(Fd fd) noexcept {
        if (added_depth_ == kConnectFdFull)
            return;
        added_[added_depth_] = fd;
        added_depth_ = added_depth_.next();
    }

    void remove(Fd fd) noexcept {
        if (removed_depth_ == kConnectFdFull)
            return;
        removed_[removed_depth_] = fd;
        removed_depth_ = removed_depth_.next();
    }

    [[nodiscard]] std::span<const Fd> added() const noexcept { return added_.prefix(added_depth_); }

    [[nodiscard]] std::span<const Fd> removed() const noexcept {
        return removed_.prefix(removed_depth_);
    }

    [[nodiscard]] bool empty() const noexcept {
        return added_depth_ == ConnectFdDepth{} && removed_depth_ == ConnectFdDepth{};
    }

private:
    using Slots = infra::IndexedArray<Fd, kConnectFdFull.size(), ConnectFdDepth>;

    Slots added_{};
    ConnectFdDepth added_depth_{};
    Slots removed_{};
    ConnectFdDepth removed_depth_{};
};

}
