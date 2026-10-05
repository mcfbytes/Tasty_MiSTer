// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
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
#include "infra/message_sum.h"
#include "os/clock.h"
#include "os/deadline.h"
#include "infra/seat.h"

namespace mister::app {

class NameConfig;

struct MglItem {
    TASTY_SEAT_EXEMPT(component);
    enum class Kind : std::uint8_t { Load, Reset, kCount };
    enum class Slot : std::uint8_t { File, Image };

    static constexpr std::size_t kPathMax = 1024;
    static constexpr std::size_t kStore = kPathMax + 8;

    struct Load {
        static constexpr Kind kKind = Kind::Load;
        Slot slot = Slot::File;
        std::uint8_t index = 0;
        std::uint16_t delay_s = 0;

        FixedStr<kPathMax, StrFit::Clip> path{};
    };
    struct Reset {
        static constexpr Kind kKind = Kind::Reset;
        std::uint16_t delay_s = 0;
        std::uint8_t hold_s = 0;
        std::uint8_t pad_ = 0;
    };
    using Alternatives = std::tuple<Load, Reset>;

    Kind kind = Kind::Load;
    std::uint8_t pad_ = 0;
    alignas(2) std::array<std::byte, kStore> store{};
};
static_assert(infra::MessageSum<MglItem> && infra::alternatives_are_total<MglItem>());
static_assert(sizeof(MglItem) == 2 + MglItem::kStore, "kind, pad and store");

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

    [[nodiscard]] Ex<void> parse(std::string_view xml, bool core_supports_mgl);

    void set_link_tx(LinkTxChannel* tx) noexcept { link_tx_ = tx; }

    void set_asks(UiRequestRing* asks) noexcept { asks_ = asks; }

    [[nodiscard]] bool set_homes(std::string_view file_home, std::string_view image_home) noexcept;

    void set_scope(CoreScope s) noexcept { scope_ = s; }
    [[nodiscard]] CoreScope scope() const noexcept { return scope_; }

    void set_names(NameConfig* names) noexcept { names_ = names; }

    void remember(std::uint8_t i, const RememberedStem& stem, RememberedSlot slot,
                  std::uint8_t ioctl_index) noexcept;
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
    [[nodiscard]] std::uint8_t current() const noexcept { return current_; }
    [[nodiscard]] const MglItem& item(std::uint8_t i) const noexcept { return items_[i]; }

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

    [[nodiscard]] std::uint16_t delay_of_(std::uint8_t i) const noexcept;

    [[nodiscard]] std::uint8_t hold_of_(std::uint8_t i) const noexcept;
    void record_publish(CorrelationTag tag) noexcept;
    void record_wire_publish(bool pushed) noexcept;

    LinkTxChannel* link_tx_ = nullptr;
    UiRequestRing* asks_ = nullptr;
    NameConfig* names_ = nullptr;

    struct RememberAt {
        RememberedSlot slot = RememberedSlot::File;
        std::uint8_t ioctl_index = 0;
    };
    std::array<std::optional<RememberAt>, kMaxItems> remember_{};
    RememberedStem stem_{};
    std::uint32_t remembered_ = 0;
    std::uint32_t remember_failures_ = 0;
    CoreScope scope_{};
    std::array<MglItem, kMaxItems> items_{};
    PathText file_home_{};
    PathText image_home_{};
    std::uint8_t count_ = 0;
    std::uint8_t current_ = 0;
    std::uint32_t publishes_ = 0;
    std::uint32_t drops_ = 0;
    CorrelationTag last_tag_{};
    std::array<CorrelationTag, kMaxItems> tags_{};
    bool armed_ = false;
    MglState state_ = MglState::Done;

    os::Deadline timer_ = os::Deadline::immediate();
};

}  // namespace mister::app
