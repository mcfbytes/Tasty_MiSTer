// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "app/link_event_facts.h"
#include "infra/error.h"
#include "infra/message_sum.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "proto/link_event.h"
#include "proto/link_router.h"
#include "proto/types.h"

namespace mister::app {

template <SeatTag Consumer>
class LinkRxChannel;

class LinkRouter final : public proto::ILinkRouter {
    TASTY_SEAT_RESIDENT(RT);

public:
    struct Channels {
        LinkRxChannel<SeatTag::Main>& main;
        LinkRxChannel<SeatTag::Ui>& ui;
        infra::OptRef<LinkRxChannel<SeatTag::Input>> input{};
    };

    explicit LinkRouter(const Channels& c) noexcept;

    LinkRouter(
        LinkRxChannel<SeatTag::Main>& main,
        LinkRxChannel<SeatTag::Ui>& ui,
        LinkRxChannel<SeatTag::Input>& input
    ) noexcept
        : LinkRouter(Channels{main, ui, infra::OptRef<LinkRxChannel<SeatTag::Input>>{input}}) {}

    LinkRouter(const LinkRouter&) = delete;
    LinkRouter& operator=(const LinkRouter&) = delete;

    [[nodiscard]] bool push(const proto::LinkEvent& e) noexcept override;
    [[nodiscard]] bool can_intern(
        proto::LinkEvent::Kind k,
        std::size_t bytes,
        unsigned chunks
    ) const noexcept override;
    [[nodiscard]] Ex<proto::RxSlabId> intern(
        proto::LinkEvent::Kind k,
        std::span<const std::uint8_t> bytes
    ) noexcept override;

    [[nodiscard]] std::uint32_t drops() const noexcept { return drops_; }

    [[nodiscard]] std::uint32_t intern_refusals() const noexcept { return intern_refusals_; }

private:
    LinkRxChannel<SeatTag::Main>& main_;
    LinkRxChannel<SeatTag::Ui>& ui_;
    infra::OptRef<LinkRxChannel<SeatTag::Input>> input_{};
    std::uint32_t drops_ = 0;
    std::uint32_t intern_refusals_ = 0;
};

static_assert(
    infra::ordinal(EventSink::kCount) == 3,
    "a new EventSink needs its channel in LinkRouter::Channels"
);

}
