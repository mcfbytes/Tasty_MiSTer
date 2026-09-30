// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <tuple>

#include "app/stream_io.h"
#include "infra/counter.h"
#include "infra/error.h"
#include "infra/fixed_str.h"
#include "infra/loan_channel.h"
#include "infra/message_sum.h"
#include "infra/seat.h"
#include "svc/file.h"
#include "svc/io_coworker.h"

namespace mister::app {

inline constexpr std::size_t kStreamRelCap = 1024;

inline constexpr std::size_t kStreamChunkBytes = 64u * 1024u;
static_assert(kStreamChunkBytes % 32u == 0u);
static_assert(kStreamChunkBytes >= 16u);

inline constexpr std::size_t kStreamDepth = 2;

struct FileStreamSlot {
    TASTY_SEAT_MEDIATOR(Any, Any);

    using Rel = FixedStr<kStreamRelCap, StrFit::Reject>;

    struct Ask {
        TASTY_SEAT_EXEMPT(component);
        enum class Kind : std::uint8_t { Open, Read, Write, Sync, Close, kCount };
        static constexpr std::size_t kStore = 2064;

        struct Head {
            std::uint32_t gen = 0;
        };
        struct Open {
            static constexpr Kind kKind = Kind::Open;
            Rel rel{};
            Rel out_rel{};
            svc::OpenMode out_mode = svc::OpenMode::Truncate;
            std::uint8_t pad_[3]{};
        };
        struct Read {
            static constexpr Kind kKind = Kind::Read;
            std::uint64_t offset = 0;
            std::uint32_t want = 0;
            std::uint8_t pad_[4]{};
        };
        struct Write {
            static constexpr Kind kKind = Kind::Write;
            std::uint64_t offset = 0;
            std::uint32_t want = 0;
            std::uint8_t pad_[4]{};
        };
        struct Sync {
            static constexpr Kind kKind = Kind::Sync;
        };
        struct Close {
            static constexpr Kind kKind = Kind::Close;
        };

        using Alternatives = std::tuple<Open, Read, Write, Sync, Close>;

        Kind kind = Kind::Close;
        std::uint8_t pad_[3]{};
        Head head{};
        alignas(8) std::array<std::byte, kStore> store{};
    };

    Ask ask{};

    std::uint64_t size = 0;
    Error err{Errc::io, 0, 0};
    std::uint32_t got = 0;
    std::uint8_t ok = 0;

    alignas(64) std::byte data[kStreamChunkBytes]{};
};
static_assert(infra::MessageSum<FileStreamSlot::Ask> &&
              infra::alternatives_are_total<FileStreamSlot::Ask>());
static_assert(sizeof(FileStreamSlot::Ask) == 2072 && alignof(FileStreamSlot::Ask) == 8);

class FileStreamService final : public svc::IIoCoworker {
    TASTY_SEAT_MEDIATOR(RT, Io);

public:
    using Chan = xthread::LoanChannel<FileStreamSlot, kStreamDepth, SeatTag::RT, SeatTag::Io>;
    using Loan = Chan::Loan;

    FileStreamService() noexcept : chan_(xthread::Polled{}) {}

    explicit FileStreamService(xthread::WakeFlag& io_wake) noexcept : chan_(io_wake) {}

    void bind_vfs(const svc::Vfs& vfs) noexcept { vfs_ = &vfs; }

    void seal_boot_pump() noexcept { sealed_ = true; }
    [[nodiscard]] bool sealed() const noexcept { return sealed_; }

    [[nodiscard]] bool arm_open(std::string_view rel) noexcept;

    [[nodiscard]] bool arm_open(std::string_view rel, std::string_view out_rel,
                                svc::OpenMode out_mode) noexcept;
    [[nodiscard]] bool arm_step(std::uint64_t off, std::uint32_t want) noexcept;

    [[nodiscard]] bool arm_write(std::uint64_t off, std::span<const std::byte> bytes) noexcept;

    [[nodiscard]] bool arm_sync() noexcept;
    [[nodiscard]] bool arm_close() noexcept;

    [[nodiscard]] Loan reap() noexcept;

    [[nodiscard]] bool has_open_stream() const noexcept { return rt_stream_open_; }

    [[nodiscard]] std::uint32_t generation() const noexcept { return gen_; }

    [[nodiscard]] std::uint32_t refusals() const noexcept { return refusals_.get(); }
    [[nodiscard]] std::uint32_t declines() const noexcept { return declines_.get(); }

    [[nodiscard]] std::uint32_t sealed_pumps() const noexcept { return sealed_pumps_.get(); }

    [[nodiscard]] std::uint32_t opens() const noexcept { return opens_.get(); }

    [[nodiscard]] std::uint32_t closes() const noexcept { return closes_.get(); }

    [[nodiscard]] std::uint32_t stales() const noexcept { return stales_.get(); }
    void count_decline() noexcept { declines_.add(1); }
    [[nodiscard]] Chan::Census census() const noexcept { return chan_.census(); }

    void serve() noexcept override;
    [[nodiscard]] bool idle() const noexcept override;

    void pump_on_caller() noexcept;

    [[nodiscard]] Chan& channel_for_test() noexcept { return chan_; }

private:
    [[nodiscard]] Loan acquire_() noexcept;
    void send_(Loan&& l) noexcept;

    const svc::Vfs* vfs_ = nullptr;
    bool sealed_ = false;
    bool rt_stream_open_ = false;
    std::uint32_t gen_ = 0;

    StreamFile src_{}, out_{};
    std::uint32_t io_gen_ = 0;
    xthread::Counter refusals_{}, declines_{}, sealed_pumps_{};
    xthread::Counter opens_{}, closes_{}, stales_{};
    Chan chan_;
};

}  // namespace mister::app
