// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <chrono>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string_view>
#include <tuple>

#include "app/link_tx_channel.h"
#include "app/path_text.h"
#include "app/remembered_path.h"
#include "app/ui_request_ring.h"
#include "app/types.h"
#include "infra/fixed_str.h"
#include "infra/error.h"
#include "infra/indexed_array.h"
#include "infra/message_sum.h"
#include "infra/path_text.h"
#include "os/clock.h"
#include "os/deadline.h"
#include "proto/types.h"
#include "infra/seat.h"
#include "app/mgl_item.h"

namespace mister::app {

class NameConfig;

enum class MglState : std::uint8_t {
    Armed,
    Dispatching,
    Advancing,
    ResetAssert,
    ResetRelease,
    Done,
};

class MglPlayer {
    TASTY_SEAT_RESIDENT(Ui);

public:
    static constexpr std::size_t kMaxItems = 6;

    struct Wiring {
        LinkTxChannel<SeatTag::Ui>& link_tx;
        UiRequestRing& asks;
        NameConfig& names;
    };

    explicit MglPlayer(Wiring w) noexcept : link_tx_(w.link_tx), asks_(w.asks), names_(w.names) {}

    void reset() noexcept;

    [[nodiscard]] Ex<void> parse(std::string_view xml, bool core_supports_mgl);

    enum class PathBase : std::uint8_t { Home, Card };

    [[nodiscard]] Ex<void> adopt(const MglItem& item, PathBase base, bool core_supports_mgl);

    [[nodiscard]] bool set_homes(std::string_view file_home, std::string_view image_home) noexcept;

    void set_scope(CoreScope s) noexcept { scope_ = s; }

    [[nodiscard]] CoreScope scope() const noexcept { return scope_; }

    void remember(
        MglItemNo i,
        const RememberedStem& stem,
        RememberedSlot slot,
        proto::IoIndex ioctl_index
    ) noexcept;

    [[nodiscard]] std::uint32_t remembered() const noexcept { return remembered_; }

    [[nodiscard]] std::uint32_t remember_failures() const noexcept { return remember_failures_; }

    void arm(const os::IClock& clock);

    [[nodiscard]] Ex<void> advance(const os::IClock& clock);

    [[nodiscard]] std::uint32_t publishes() const noexcept { return publishes_; }

    [[nodiscard]] std::uint32_t drops() const noexcept { return drops_; }

    [[nodiscard]] CorrelationTag last_tag() const noexcept { return last_tag_; }

    [[nodiscard]] bool replay_refused(CorrelationTag tag, const os::IClock& clock);
    [[nodiscard]] bool owns(CorrelationTag tag) const noexcept;
    static constexpr unsigned kBusyReplayMs = 100;

    [[nodiscard]] bool done() const noexcept { return state_ == MglState::Done; }

    [[nodiscard]] MglState state() const noexcept { return state_; }

    [[nodiscard]] std::uint8_t count() const noexcept { return count_; }

    [[nodiscard]] MglItemNo current() const noexcept { return current_; }

    [[nodiscard]] const MglItem& item(MglItemNo i) const noexcept { return entries_[i].item; }

    void on(const MglItem::Load&) noexcept {
        TASTY_SEAT_BODY(MglPlayer);
        state_ = MglState::Dispatching;
    }

    void on(const MglItem::Reset&) noexcept {
        TASTY_SEAT_BODY(MglPlayer);
        state_ = MglState::ResetAssert;
    }

    void misrouted(const MglItem&) noexcept { TASTY_SEAT_BODY(MglPlayer); }

private:
    void dispatch_current();

    [[nodiscard]] Ex<void> publish_load_();

    [[nodiscard]] DelaySeconds delay_of_(MglItemNo i) const noexcept;

    [[nodiscard]] std::uint8_t hold_of_(MglItemNo i) const noexcept;
    void record_publish(CorrelationTag tag) noexcept;
    void record_wire_publish(bool pushed) noexcept;

    LinkTxChannel<SeatTag::Ui>& link_tx_;
    UiRequestRing& asks_;
    NameConfig& names_;

    struct RememberAt {
        RememberedSlot slot = RememberedSlot::File;
        proto::IoIndex ioctl_index{};
    };

    struct PlaylistEntry {
        MglItem item{};
        std::optional<RememberAt> remember{};
        CorrelationTag tag{};
    };

    RememberedStem stem_{};
    std::uint32_t remembered_ = 0;
    std::uint32_t remember_failures_ = 0;
    CoreScope scope_{};
    infra::IndexedArray<PlaylistEntry, kMaxItems, MglItemNo> entries_{};
    PathText file_home_{};
    PathText image_home_{};
    PathBase base_ = PathBase::Home;
    std::uint8_t count_ = 0;
    MglItemNo current_{};
    std::uint32_t publishes_ = 0;
    std::uint32_t drops_ = 0;
    CorrelationTag last_tag_{};
    bool armed_ = false;
    MglState state_ = MglState::Done;

    os::Deadline timer_ = os::Deadline::immediate();
};

}
