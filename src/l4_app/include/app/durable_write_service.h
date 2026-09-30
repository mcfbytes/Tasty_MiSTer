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

enum class WriteKind : std::uint8_t { Silent = 0, Save = 1 };

inline constexpr std::size_t kWriteLaneBytes = 8192;
inline constexpr std::size_t kWriteDepth = 4;

inline constexpr std::size_t kSaveLaneBytes = 1024u * 1024u;
inline constexpr std::size_t kSaveDepth = 1;

struct WriteSlot {
    TASTY_SEAT_MEDIATOR(Any, Any);
    FixedStr<kWriteRelCap, StrFit::Reject> rel{};
    std::uint32_t len = 0;
    WriteKind kind = WriteKind::Silent;
    std::uint8_t ok = 0;
    std::byte data[kWriteLaneBytes]{};
};

struct SaveSlot {
    TASTY_SEAT_MEDIATOR(Any, Any);
    FixedStr<kWriteRelCap, StrFit::Reject> rel{};
    std::uint32_t len = 0;
    WriteKind kind = WriteKind::Silent;
    std::uint8_t ok = 0;
    std::byte data[kSaveLaneBytes]{};
};

class DurableWriteService final : public svc::IIoCoworker, public svc::IPendingWrites {
    TASTY_SEAT_MEDIATOR(RT, Io);

public:
    using Chan = xthread::LoanChannel<WriteSlot, kWriteDepth, SeatTag::RT, SeatTag::Io>;
    using SaveChan = xthread::LoanChannel<SaveSlot, kSaveDepth, SeatTag::RT, SeatTag::Io>;

    explicit DurableWriteService(const svc::Vfs& vfs) noexcept
        : vfs_(&vfs), chan_(xthread::Polled{}), save_chan_(xthread::Polled{}) {}

    DurableWriteService(const svc::Vfs& vfs, xthread::WakeFlag& io_wake) noexcept
        : vfs_(&vfs), chan_(io_wake), save_chan_(io_wake) {}

    [[nodiscard]] bool arm(std::string_view rel, std::span<const std::byte> bytes,
                           WriteKind kind = WriteKind::Silent) noexcept;

    struct WriteDone {
        WriteKind kind = WriteKind::Silent;
        bool ok = false;
    };
    [[nodiscard]] std::optional<WriteDone> reap() noexcept;

    [[nodiscard]] std::span<const std::byte> peek(std::string_view rel) const noexcept override;

    [[nodiscard]] std::uint32_t refusals() const noexcept { return refusals_.get(); }
    [[nodiscard]] std::uint32_t failures() const noexcept { return failures_.get(); }
    [[nodiscard]] std::uint32_t writes() const noexcept { return writes_.get(); }
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

    [[nodiscard]] bool arm_save_(std::string_view rel, std::span<const std::byte> bytes) noexcept;

    const svc::Vfs* vfs_;
    std::uint32_t seq_ = 0;
    Chan chan_;
    Shadow shadow_[kWriteDepth]{};
    SaveChan save_chan_;
    std::uint8_t saves_armed_ = 0;
    xthread::Counter refusals_{}, failures_{}, writes_{};
};

}  // namespace mister::app
