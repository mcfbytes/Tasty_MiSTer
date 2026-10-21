// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <type_traits>

#include "app/ui_request.h"
#include "infra/inbox.h"
#include "infra/message_sum.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "infra/wake_flag.h"

namespace mister::app {

class UiRequestRing {
    TASTY_SEAT_MEDIATOR(Ui, Main);

public:
    static constexpr std::size_t kSlots = 8;
    static constexpr std::size_t kPending = 8;

    struct Pending {
        TASTY_SEAT_RESIDENT(Ui);
        CorrelationTag tag{};
        UiRequest::Kind which{};
        proto::IoIndex index{};
        char slot = 0;
        bool game = false;
        PathText path{};
    };

    UiRequestRing() noexcept : ring_(xthread::Polled{}) {}

    explicit UiRequestRing(xthread::WakeFlag& wake) noexcept : ring_(wake) {}

    UiRequestRing(const UiRequestRing&) = delete;
    UiRequestRing& operator=(const UiRequestRing&) = delete;

    template <class A>
        requires infra::AlternativeOf<A, UiRequest>
    [[nodiscard]] CorrelationTag push(const A& alt) noexcept {
        const CorrelationTag tag{ui_.next_tag};
        ui_.next_tag = (ui_.next_tag == 0xFFFF'FFFFu) ? 1u : ui_.next_tag + 1u;
        if (!ring_.push(infra::make<UiRequest>(alt, UiRequest::Head{tag})))
            return kUncaused;
        if constexpr (std::is_same_v<A, UiRequest::LoadFile>) {
            ui_.last_load = alt.path;
            ++ui_.load_asks;
            note_({tag, A::kKind, alt.index, 'F', true, alt.path});
        } else if constexpr (std::is_same_v<A, UiRequest::LoadFileByDigit>) {
            note_({tag, A::kKind, proto::IoIndex{alt.digit.v}, 'F', true, alt.path});
        } else if constexpr (std::is_same_v<A, UiRequest::MountImage>) {
            note_({tag, A::kKind, alt.index, 'S', true, alt.path});
        } else if constexpr (std::is_same_v<A, UiRequest::UnmountImage>) {
            note_({tag, A::kKind, alt.index, 'S', false, PathText{}});
        } else if constexpr (std::is_same_v<A, UiRequest::LoadCore>) {
            note_({tag, A::kKind, proto::IoIndex{}, 0, false, alt.path});
        }
        return tag;
    }

    [[nodiscard]] infra::OptRef<const Pending> pending(CorrelationTag tag) const noexcept;
    void forget(CorrelationTag tag) noexcept;

    [[nodiscard]] std::uint32_t pending_evicted() const noexcept { return ui_.pending_evicted; }

    [[nodiscard]] std::string_view last_load_path() const noexcept { return ui_.last_load.view(); }

    [[nodiscard]] std::uint32_t load_asks() const noexcept { return ui_.load_asks; }

    [[nodiscard]] std::optional<UiRequest> pop() noexcept { return ring_.pop(); }

    [[nodiscard]] std::size_t size() const noexcept { return ring_.size(); }

private:
    struct Ui {
        std::uint32_t next_tag = 1;
        PathText last_load{};
        std::uint32_t load_asks = 0;
        std::array<Pending, kPending> pending{};
        std::uint32_t pending_evicted = 0;
    };

    xthread::Inbox<UiRequest, kSlots> ring_;

    Ui ui_;

    void note_(const Pending& p) noexcept;
};

inline infra::OptRef<const UiRequestRing::Pending> UiRequestRing::pending(
    CorrelationTag tag
) const noexcept {
    if (tag == kUncaused)
        return std::nullopt;
    for (const Pending& p : ui_.pending)
        if (p.tag == tag)
            return infra::OptRef<const Pending>{p};
    return std::nullopt;
}

inline void UiRequestRing::forget(CorrelationTag tag) noexcept {
    if (tag == kUncaused)
        return;
    for (Pending& p : ui_.pending)
        if (p.tag == tag)
            p.tag = kUncaused;
}

inline void UiRequestRing::note_(const Pending& p) noexcept {
    Pending* at = &ui_.pending[0];
    for (Pending& e : ui_.pending) {
        if (e.tag == kUncaused) {
            at = &e;
            break;
        }
        if (ui_.next_tag - e.tag.v > ui_.next_tag - at->tag.v)
            at = &e;
    }
    if (at->tag != kUncaused)
        ++ui_.pending_evicted;
    *at = p;
}

}
