// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "infra/message_sum.h"
#include "infra/seat.h"
#include "proto/link_event.h"
#include "proto/user_io/poll_shape.h"

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
    {proto::LinkEvent::Kind::PolledWord, EventSink::Ui},
});
static_assert(
    infra::rows_are_ordinal(kLinkEventFacts),
    "kLinkEventFacts is one row per LinkEvent kind, in ordinal order"
);

[[nodiscard]] constexpr EventSink sink_of(SeatTag seat) noexcept {
    if (seat == SeatTag::Ui)
        return EventSink::Ui;
    if (seat == SeatTag::Input)
        return EventSink::Input;
    return EventSink::kCount;
}

template <std::size_t N>
consteval bool poll_rows_name_event_sinks(
    const std::array<proto::user_io::PollShapeRow, N>& rows,
    EventSink kind
) {
    for (const proto::user_io::PollShapeRow& r : rows) {
        if (sink_of(r.sink) == EventSink::kCount)
            return false;
    }
    return N != 0 && sink_of(rows[0].sink) == kind;
}

static_assert(
    poll_rows_name_event_sinks(
        proto::user_io::kPollShapes,
        kLinkEventFacts[infra::ordinal(proto::LinkEvent::Kind::PolledWord)].sink
    ),
    "every poll row's seat drains an event channel; PolledWord's row names the first's"
);

[[nodiscard]] constexpr EventSink poll_sink(proto::user_io::PollShape s) noexcept {
    const std::size_t i = infra::ordinal(s);
    return i < proto::user_io::kPollShapes.size() ? sink_of(proto::user_io::kPollShapes[i].sink)
                                                  : EventSink::kCount;
}

template <class A>
[[nodiscard]] consteval bool routed_to(EventSink seat) {
    if constexpr (std::is_same_v<A, proto::LinkEvent::PolledWord>) {
        for (const proto::user_io::PollShapeRow& r : proto::user_io::kPollShapes)
            if (sink_of(r.sink) == seat)
                return true;
        return false;
    } else {
        return kLinkEventFacts[infra::ordinal(A::kKind)].sink == seat;
    }
}

[[nodiscard]] constexpr EventSink sink_for(proto::LinkEvent::Kind k) noexcept {
    const std::size_t i = infra::ordinal(k);
    return i < kLinkEventFacts.size() ? kLinkEventFacts[i].sink : EventSink::kCount;
}

[[nodiscard]] inline EventSink sink_for(const proto::LinkEvent& e) noexcept {
    if (const auto w = infra::as<proto::LinkEvent::PolledWord>(e))
        return poll_sink(w->shape);
    return sink_for(infra::kind_of(e));
}

}
