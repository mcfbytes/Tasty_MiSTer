// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "app/link_event_facts.h"
#include "infra/error.h"
#include "infra/message_sum.h"
#include "infra/seat.h"
#include "proto/link_event.h"
#include "proto/link_router.h"
#include "proto/types.h"

namespace mister::app {

class LinkRxChannel;

class LinkRouter final : public proto::ILinkRouter {
    TASTY_SEAT_RESIDENT(RT);

public:
    struct Channels {
        LinkRxChannel* main = nullptr;
        LinkRxChannel* ui = nullptr;
        LinkRxChannel* input = nullptr;
    };
    explicit LinkRouter(const Channels& c) noexcept;

    LinkRouter(LinkRxChannel& main, LinkRxChannel& ui, LinkRxChannel& input) noexcept
        : LinkRouter(Channels{.main = &main, .ui = &ui, .input = &input}) {}
    LinkRouter(const LinkRouter&) = delete;
    LinkRouter& operator=(const LinkRouter&) = delete;

    [[nodiscard]] bool push(const proto::LinkEvent& e) noexcept override;
    [[nodiscard]] bool can_intern(proto::LinkEvent::Kind k, std::size_t bytes,
                                  unsigned chunks) const noexcept override;
    [[nodiscard]] Ex<proto::RxSlabId> intern(proto::LinkEvent::Kind k,
                                             std::span<const std::uint8_t> bytes) noexcept override;

    [[nodiscard]] std::uint32_t drops() const noexcept { return drops_; }
    [[nodiscard]] std::uint32_t intern_refusals() const noexcept { return intern_refusals_; }

private:
    [[nodiscard]] LinkRxChannel* channel_(proto::LinkEvent::Kind k) const noexcept;
    std::array<LinkRxChannel*, infra::ordinal(EventSink::kCount)> chans_{};
    std::uint32_t drops_ = 0;
    std::uint32_t intern_refusals_ = 0;
};

static_assert(infra::ordinal(EventSink::kCount) == 3,
              "a new EventSink needs its channel in LinkRouter::Channels");

}  // namespace mister::app
