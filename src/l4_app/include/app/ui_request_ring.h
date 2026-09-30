// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <optional>
#include <string_view>
#include <type_traits>

#include "app/ui_request.h"
#include "infra/inbox.h"
#include "infra/message_sum.h"
#include "infra/seat.h"
#include "infra/wake_flag.h"

namespace mister::app {

class UiRequestRing {
    TASTY_SEAT_MEDIATOR(Ui, Any);

public:
    static constexpr std::size_t kSlots = 8;

    UiRequestRing() noexcept : ring_(xthread::Polled{}) {}

    explicit UiRequestRing(xthread::WakeFlag& wake) noexcept : ring_(wake) {}
    UiRequestRing(const UiRequestRing&) = delete;
    UiRequestRing& operator=(const UiRequestRing&) = delete;

    template <class A>
        requires infra::AlternativeOf<A, UiRequest>
    [[nodiscard]] CorrelationTag push(const A& alt) noexcept {
        const CorrelationTag tag{next_tag_};
        next_tag_ = (next_tag_ == 0xFFFF'FFFFu) ? 1u : next_tag_ + 1u;
        if (!ring_.push(infra::make<UiRequest>(alt, UiRequest::Head{tag}))) return kUncaused;
        if constexpr (std::is_same_v<A, UiRequest::LoadFile>) {
            last_load_ = alt.path;
            ++load_asks_;
        }
        return tag;
    }

    [[nodiscard]] std::string_view last_load_path() const noexcept { return last_load_.view(); }
    [[nodiscard]] std::uint32_t load_asks() const noexcept { return load_asks_; }

    [[nodiscard]] std::optional<UiRequest> pop() noexcept { return ring_.pop(); }
    [[nodiscard]] std::size_t size() const noexcept { return ring_.size(); }

private:
    xthread::Inbox<UiRequest, kSlots> ring_;

    std::uint32_t next_tag_ = 1;
    PathText last_load_{};
    std::uint32_t load_asks_ = 0;
};

}  // namespace mister::app
