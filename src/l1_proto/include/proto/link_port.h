// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <tuple>

#include "infra/error.h"
#include "infra/message_sum.h"
#include "infra/seat.h"
#include "proto/link_event.h"
#include "proto/link_router.h"
#include "proto/types.h"

namespace mister::proto {

template <infra::AlternativeOf<LinkEvent>... A>
class LinkPort {
    TASTY_SEAT_RESIDENT(RT);

public:
    using Emits = std::tuple<A...>;
    explicit LinkPort(ILinkRouter& r) noexcept : r_(&r) {}

    template <class X>
        requires(std::same_as<X, A> || ...)
    [[nodiscard]] bool push(const X& x) noexcept {
        return r_->push(infra::make<LinkEvent>(x));
    }
    template <class X>
        requires(std::same_as<X, A> || ...)
    [[nodiscard]] bool can_intern(std::size_t bytes, unsigned chunks) const noexcept {
        return r_->can_intern(X::kKind, bytes, chunks);
    }
    template <class X>
        requires(std::same_as<X, A> || ...)
    [[nodiscard]] Ex<RxSlabId> intern(std::span<const std::uint8_t> bytes) noexcept {
        return r_->intern(X::kKind, bytes);
    }

private:
    ILinkRouter* r_;
};

}  // namespace mister::proto
