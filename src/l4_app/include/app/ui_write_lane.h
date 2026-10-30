// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "infra/error.h"
#include "infra/fixed_str.h"
#include "infra/loan_channel.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "infra/wake_flag.h"
#include "svc/io_coworker.h"

namespace mister::svc {
class Vfs;
}

namespace mister::app {

class IWriteVerdict;

class UiWriteLane final : public svc::IIoCoworker {
    TASTY_SEAT_MEDIATOR(Ui, Io);

public:
    static constexpr std::size_t kRelCap = 256;

    static constexpr std::size_t kBytes = 72u * 1024u;

    static constexpr std::size_t kShadowBytes = 24u * 1024u;
    static constexpr std::size_t kDepth = 4;

    struct Slot {
        TASTY_SEAT_MEDIATOR(Any, Any);
        FixedStr<kRelCap, StrFit::Reject> rel{};
        std::uint32_t len = 0;
        std::uint8_t ok = 0;
        std::byte data[kBytes]{};
    };

    using Chan = xthread::LoanChannel<Slot, kDepth, SeatTag::Ui, SeatTag::Io>;

    explicit UiWriteLane(const svc::Vfs& vfs) noexcept
        : vfs_(&vfs),
          chan_(xthread::Polled{}, xthread::Polled{}) {}

    UiWriteLane(const svc::Vfs& vfs, xthread::WakeFlag& io_wake) noexcept
        : vfs_(&vfs),
          chan_(io_wake, xthread::Polled{}) {}

    UiWriteLane(const UiWriteLane&) = delete;
    UiWriteLane& operator=(const UiWriteLane&) = delete;
    UiWriteLane(UiWriteLane&&) = delete;
    UiWriteLane& operator=(UiWriteLane&&) = delete;
    ~UiWriteLane() override = default;

    [[nodiscard]] Ex<void> arm(
        std::string_view rel,
        std::span<const std::byte> bytes,
        infra::OptRef<IWriteVerdict> verdict = {}
    ) noexcept;

    void tick() noexcept;

    [[nodiscard]] Ex<std::span<const std::byte>> peek(std::string_view rel) const noexcept;

    [[nodiscard]] std::uint32_t refusals() const noexcept { return ui_.refusals; }

    [[nodiscard]] std::uint32_t failures() const noexcept { return ui_.failures; }

    [[nodiscard]] std::uint32_t writes() const noexcept { return ui_.writes; }

    [[nodiscard]] std::size_t in_flight() const noexcept;

    void serve() noexcept override;
    [[nodiscard]] bool idle() const noexcept override;

private:
    struct Shadow {
        FixedStr<kRelCap, StrFit::Reject> rel{};
        std::uint32_t len = 0;
        std::uint32_t seq = 0;
        bool live = false;
        bool held = false;
        infra::OptRef<IWriteVerdict> verdict{};
        std::byte data[kShadowBytes]{};
    };

    struct Ui {
        std::uint32_t seq = 0;
        Shadow shadow[kDepth]{};
        std::uint32_t refusals = 0;
        std::uint32_t failures = 0;
        std::uint32_t writes = 0;
    };

    const svc::Vfs* vfs_;
    Ui ui_{};
    Chan chan_;
};

}
