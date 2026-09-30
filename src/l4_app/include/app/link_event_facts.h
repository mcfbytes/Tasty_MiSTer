// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "infra/message_sum.h"
#include "proto/link_event.h"

namespace mister::app {

enum class EventSink : std::uint8_t { Main, Ui, Input, kCount };

struct LinkEventFacts {
    proto::LinkEvent::Kind kind;
    EventSink sink;
};

inline constexpr auto kLinkEventFacts = std::to_array<LinkEventFacts>({
    {proto::LinkEvent::Kind::ReadyEdge, EventSink::Main},
    {proto::LinkEvent::Kind::ButtonLevel, EventSink::Ui},
    {proto::LinkEvent::Kind::BlockRequest, EventSink::Main},
    {proto::LinkEvent::Kind::ConfStr, EventSink::Main},
    {proto::LinkEvent::Kind::SaveBytes, EventSink::Main},
    {proto::LinkEvent::Kind::Ps2Control, EventSink::Input},
    {proto::LinkEvent::Kind::CoreMade, EventSink::Main},
    {proto::LinkEvent::Kind::IdentityMatched, EventSink::Main},
    {proto::LinkEvent::Kind::IdentityMismatch, EventSink::Main},
    {proto::LinkEvent::Kind::OsdMask, EventSink::Ui},
    {proto::LinkEvent::Kind::Ps2ControlEnded, EventSink::Input},
    {proto::LinkEvent::Kind::StartRefused, EventSink::Main},
    {proto::LinkEvent::Kind::RebootQuiesced, EventSink::Main},
});
static_assert(infra::rows_are_ordinal(kLinkEventFacts),
              "kLinkEventFacts is one row per LinkEvent kind, in ordinal order");

[[nodiscard]] constexpr EventSink sink_for(proto::LinkEvent::Kind k) noexcept {
    const std::size_t i = infra::ordinal(k);
    return i < kLinkEventFacts.size() ? kLinkEventFacts[i].sink : EventSink::kCount;
}

}  // namespace mister::app
