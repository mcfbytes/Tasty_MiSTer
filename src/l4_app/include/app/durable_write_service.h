// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "app/types.h"
#include "infra/counter.h"
#include "infra/fixed_str.h"
#include "infra/loan_channel.h"
#include "infra/seat.h"
#include "svc/io_coworker.h"
#include "svc/pending_writes.h"
#include "svc/vfs.h"

namespace mister::app {

inline constexpr std::size_t kWriteRelCap = 256;

inline constexpr std::size_t kWriteLaneBytes = 8192;
inline constexpr std::size_t kWriteDepth = 4;

struct WriteSlot {
    TASTY_SEAT_MEDIATOR(Any, Any);
    FixedStr<kWriteRelCap, StrFit::Reject> rel{};
    std::uint32_t len = 0;
    std::uint8_t ok = 0;
    std::byte data[kWriteLaneBytes]{};
};

class DurableWriteService final : public svc::IIoCoworker, public svc::IPendingWrites {
    TASTY_SEAT_MEDIATOR(RT, Io);

public:
    using Chan = xthread::LoanChannel<WriteSlot, kWriteDepth, SeatTag::RT, SeatTag::Io>;

    explicit DurableWriteService(const svc::Vfs& vfs) noexcept
        : vfs_(&vfs),
          chan_(xthread::Polled{}) {}

    DurableWriteService(const svc::Vfs& vfs, xthread::WakeFlag& io_wake) noexcept
        : vfs_(&vfs),
          chan_(io_wake) {}

    [[nodiscard]] bool arm(std::string_view rel, std::span<const std::byte> bytes) noexcept;

    struct WriteDone {
        bool ok = false;
    };

    [[nodiscard]] std::optional<WriteDone> reap() noexcept;

    [[nodiscard]] std::span<const std::byte> peek(std::string_view rel) const noexcept override;

    [[nodiscard]] std::uint32_t refusals() const noexcept { return rt_.refusals.get(); }

    [[nodiscard]] std::uint32_t failures() const noexcept { return rt_.failures.get(); }

    [[nodiscard]] std::uint32_t writes() const noexcept { return rt_.writes.get(); }

    [[nodiscard]] std::size_t in_flight() const noexcept;

    void serve() noexcept override;
    [[nodiscard]] bool idle() const noexcept override;

    void drain_on_caller() noexcept;

private:
    struct Shadow {
        FixedStr<kWriteRelCap, StrFit::Reject> rel{};
        std::uint32_t len = 0;
        std::uint32_t seq = 0;
        bool live = false;
        std::byte data[kWriteLaneBytes]{};
    };

    struct Rt {
        std::uint32_t seq = 0;
        Shadow shadow[kWriteDepth]{};
        xthread::Counter refusals{}, failures{}, writes{};
    };

    const svc::Vfs* vfs_;

    Rt rt_{};
    Chan chan_;
};

}
