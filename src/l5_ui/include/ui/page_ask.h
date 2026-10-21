// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string_view>

#include "app/ui_request_ring.h"
#include "infra/opt_ref.h"
#include "infra/message_sum.h"

namespace mister::ui {

template <class A>
    requires infra::AlternativeOf<A, app::UiRequest>
[[nodiscard]] inline bool push_ask(app::UiRequestRing* ring, const A& alt) noexcept {
    return ring != nullptr && static_cast<bool>(ring->push(alt));
}

template <class A>
    requires infra::AlternativeOf<A, app::UiRequest> && requires(A a) { a.path; }
[[nodiscard]] inline bool push_ask(
    app::UiRequestRing* ring,
    A alt,
    std::string_view path
) noexcept {
    if (!alt.path.assign(path))
        return false;
    return push_ask(ring, alt);
}

template <class A>
    requires infra::AlternativeOf<A, app::UiRequest>
[[nodiscard]] inline bool push_ask(infra::OptRef<app::UiRequestRing> ring, const A& alt) noexcept {
    return ring && static_cast<bool>(ring->push(alt));
}

template <class A>
    requires infra::AlternativeOf<A, app::UiRequest> && requires(A a) { a.path; }
[[nodiscard]] inline bool push_ask(
    infra::OptRef<app::UiRequestRing> ring,
    A alt,
    std::string_view path
) noexcept {
    if (!alt.path.assign(path))
        return false;
    return push_ask(ring, alt);
}

}
