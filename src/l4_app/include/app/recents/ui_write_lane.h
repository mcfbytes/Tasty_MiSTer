// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "app/recents/verdict_sinks.h"
#include "infra/error.h"
#include "infra/fixed_str.h"
#include "infra/indexed_array.h"
#include "infra/xthread/loan_channel.h"
#include "infra/xthread/polled.h"
#include "infra/xthread/pool_slot.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "infra/xthread/wake_flag.h"
#include "svc/card_writer.h"
#include "svc/io_coworker.h"

namespace mister::svc {
class Vfs;
}

namespace mister::app::recents {

class UiWriteLane final : public svc::IIoCoworker {
    TASTY_SEAT_MEDIATOR(Ui, Io);

public:
    static constexpr std::size_t kRelCap = 256;

    static constexpr std::size_t kBytes = 72u * 1024u;

    static constexpr std::size_t kShadowBytes = 24u * 1024u;
    static constexpr std::size_t kDepth = 4;

    struct Target {
        std::string_view rel;
        std::string_view retire{};
    };

    struct Slot {
        TASTY_SEAT_MEDIATOR(Any, Any);
        FixedStr<kRelCap, StrFit::Reject> rel{};
        FixedStr<kRelCap, StrFit::Reject> retire{};
        std::uint32_t len = 0;

        std::optional<Error> fault{};
        std::byte data[kBytes]{};
    };

    using Chan = xthread::LoanChannel<Slot, kDepth, SeatTag::Ui, SeatTag::Io>;

    explicit UiWriteLane(const svc::Vfs& vfs) noexcept
        : card_(vfs),
          chan_(xthread::Polled{}, xthread::Polled{}) {}

    UiWriteLane(const svc::Vfs& vfs, xthread::WakeFlag& io_wake) noexcept
        : card_(vfs),
          chan_(io_wake, xthread::Polled{}) {}

    UiWriteLane(const UiWriteLane&) = delete;
    UiWriteLane& operator=(const UiWriteLane&) = delete;
    UiWriteLane(UiWriteLane&&) = delete;
    UiWriteLane& operator=(UiWriteLane&&) = delete;
    ~UiWriteLane() override = default;

    [[nodiscard]] Ex<void> arm(
        std::string_view rel,
        std::span<const std::byte> bytes,
        VerdictSink sink = VerdictSink::None
    ) noexcept;

    [[nodiscard]] Ex<void> arm(
        Target target,
        std::span<const std::byte> bytes,
        VerdictSink sink = VerdictSink::None
    ) noexcept;

    void tick(VerdictSinks sinks) noexcept;

    [[nodiscard]] Ex<std::span<const std::byte>> peek(std::string_view rel) const noexcept;

    [[nodiscard]] std::uint32_t refusals() const noexcept { return ui_.refusals; }

    [[nodiscard]] std::uint32_t failures() const noexcept { return ui_.failures; }

    [[nodiscard]] std::uint32_t writes() const noexcept { return ui_.writes; }

    [[nodiscard]] std::optional<Error> last_fault() const noexcept { return ui_.last_fault_; }

    [[nodiscard]] std::size_t in_flight() const noexcept;

    static constexpr svc::LaneClass kLaneClass = svc::LaneClass::Bulk;

    void serve() noexcept override;
    [[nodiscard]] bool idle() const noexcept override;

    [[nodiscard]] Chan& channel_for_test() noexcept { return chan_; }

private:
    struct Shadow {
        FixedStr<kRelCap, StrFit::Reject> rel{};
        std::uint32_t len = 0;
        std::uint32_t seq = 0;
        bool live = false;
        bool held = false;
        VerdictSink sink = VerdictSink::None;
        std::byte data[kShadowBytes]{};
    };

    struct Ui {
        std::uint32_t seq = 0;
        infra::IndexedArray<Shadow, kDepth, xthread::PoolSlot> shadow{};
        std::uint32_t refusals = 0;
        std::uint32_t failures = 0;
        std::uint32_t writes = 0;
        std::optional<Error> last_fault_{};
    };

    svc::CardWriter card_;
    Ui ui_{};
    Chan chan_;
};

}
