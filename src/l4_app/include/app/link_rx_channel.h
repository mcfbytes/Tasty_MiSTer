// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

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

class LinkRxChannel {
    TASTY_SEAT_MEDIATOR(RT, Any);

public:
    using Bytes = LinkBytes<16, 8192>;

    LinkRxChannel() noexcept : rx_(xthread::Polled{}) {}

    explicit LinkRxChannel(xthread::WakeFlag& wake) noexcept : rx_(wake) {}
    LinkRxChannel(const LinkRxChannel&) = delete;
    LinkRxChannel& operator=(const LinkRxChannel&) = delete;

    [[nodiscard]] Ex<proto::RxSlabId> intern(std::span<const std::uint8_t> bytes) noexcept {
        return bytes_.intern(bytes, rx_.pushed(), rx_.popped()).transform([](std::uint16_t v) {
            return proto::RxSlabId{v};
        });
    }

    [[nodiscard]] bool can_intern(std::size_t bytes, unsigned chunks) const noexcept {
        if (rx_.size() >= proto::kLinkRxCapacity) return false;
        return bytes_.would_accept(bytes, chunks, rx_.popped());
    }

    template <class A>
        requires infra::AlternativeOf<A, proto::LinkEvent>
    [[nodiscard]] bool push(const A& alt) noexcept {
        return push(infra::make<proto::LinkEvent>(alt));
    }
    [[nodiscard]] bool push(const proto::LinkEvent& ev) noexcept { return rx_.push(ev); }

    void request_wake() noexcept { rx_.kick(); }

    [[nodiscard]] std::optional<proto::LinkEvent> pop() noexcept { return rx_.pop(); }
    [[nodiscard]] std::span<const std::uint8_t> bytes(proto::RxSlabId id) const noexcept {
        return bytes_.get(id.v);
    }

    [[nodiscard]] std::size_t size() const noexcept { return rx_.size(); }

private:
    xthread::Inbox<proto::LinkEvent, proto::kLinkRxCapacity> rx_;
    Bytes bytes_{};
};

}  // namespace mister::app
