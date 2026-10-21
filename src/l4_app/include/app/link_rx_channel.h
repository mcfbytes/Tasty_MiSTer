// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "app/link_bytes.h"
#include "infra/inbox.h"
#include "infra/message_sum.h"
#include "infra/seat.h"
#include "infra/wake_flag.h"
#include "proto/link_event.h"

namespace mister::app {

inline constexpr std::size_t kLinkRxSlabBytes = 8192;

template <SeatTag Consumer>
class LinkRxChannel {
    TASTY_SEAT_MEDIATOR(RT, Any);
    static_assert(
        Consumer != SeatTag::RT && Consumer != SeatTag::Unbound,
        "the consumer is a named seat other than T-RT, which pushes"
    );

public:
    using Bytes = LinkBytes<16, kLinkRxSlabBytes, SeatTag::RT, Consumer>;

    LinkRxChannel() noexcept : rx_(xthread::Polled{}) {}

    explicit LinkRxChannel(xthread::WakeFlag& wake) noexcept : rx_(wake) {}

    LinkRxChannel(const LinkRxChannel&) = delete;
    LinkRxChannel& operator=(const LinkRxChannel&) = delete;

    [[nodiscard]] Ex<proto::RxSlabId> intern(std::span<const std::uint8_t> bytes) noexcept {
        TASTY_SEAT_BODY_MEDIATOR(LinkRxChannel, A);
        const Ex<std::uint16_t> id = bytes_.intern(bytes);
        if (!id)
            return std::unexpected(id.error());
        return proto::RxSlabId{*id};
    }

    [[nodiscard]] bool can_intern(std::size_t bytes, unsigned chunks) const noexcept {
        TASTY_SEAT_BODY_MEDIATOR(LinkRxChannel, A);
        if (rx_.size() >= proto::kLinkRxCapacity)
            return false;
        return bytes_.would_accept(bytes, chunks);
    }

    template <class A>
        requires infra::AlternativeOf<A, proto::LinkEvent>
    [[nodiscard]] bool push(const A& alt) noexcept {
        return push(infra::make<proto::LinkEvent>(alt));
    }

    [[nodiscard]] bool push(const proto::LinkEvent& ev) noexcept {
        TASTY_SEAT_BODY_MEDIATOR(LinkRxChannel, A);
        bytes_.send_staged(rx_.pushed());
        return rx_.push(ev);
    }

    void request_wake() noexcept {
        TASTY_SEAT_BODY_MEDIATOR(LinkRxChannel, A);
        rx_.kick();
    }

    [[nodiscard]] std::optional<proto::LinkEvent> pop() noexcept {
        bytes_.release_before(rx_.popped());
        auto ev = rx_.pop();
        if (ev)
            bytes_.take_sent();
        return ev;
    }

    [[nodiscard]] std::span<const std::uint8_t> bytes(proto::RxSlabId id) const noexcept {
        return bytes_.get(id.v);
    }

    [[nodiscard]] std::size_t size() const noexcept { return rx_.size(); }

private:
    xthread::Inbox<proto::LinkEvent, proto::kLinkRxCapacity> rx_;
    Bytes bytes_{};
};

}
