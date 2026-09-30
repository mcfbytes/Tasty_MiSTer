// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <utility>

#include "app/link_event_facts.h"
#include "infra/message_sum.h"
#include "proto/link_event.h"

namespace mister::app {

template <EventSink Seat>
struct LinkEventRoutes {
    template <class A>
    static constexpr bool routed = kLinkEventFacts[infra::ordinal(A::kKind)].sink == Seat;
};

template <class S>
concept LinkEventMainSink = requires(S& s, const proto::LinkEvent& m) {
    s.on(std::declval<const proto::LinkEvent::ReadyEdge&>());
    s.on(std::declval<const proto::LinkEvent::BlockRequest&>());
    s.on(std::declval<const proto::LinkEvent::IdentityMatched&>());
    s.on(std::declval<const proto::LinkEvent::IdentityMismatch&>());
    s.on(std::declval<const proto::LinkEvent::ConfStr&>());
    s.on(std::declval<const proto::LinkEvent::SaveBytes&>());
    s.on(std::declval<const proto::LinkEvent::CoreMade&>());
    s.on(std::declval<const proto::LinkEvent::StartRefused&>());
    s.on(std::declval<const proto::LinkEvent::RebootQuiesced&>());
    s.misrouted(m);
};

template <class S>
concept LinkEventUiSink = requires(S& s, const proto::LinkEvent& m) {
    s.on(std::declval<const proto::LinkEvent::ButtonLevel&>());
    s.on(std::declval<const proto::LinkEvent::OsdMask&>());
    s.misrouted(m);
};

template <class S>
concept LinkEventInputSink = requires(S& s, const proto::LinkEvent& m) {
    s.on(std::declval<const proto::LinkEvent::Ps2Control&>());
    s.on(std::declval<const proto::LinkEvent::Ps2ControlEnded&>());
    s.misrouted(m);
};

}  // namespace mister::app
