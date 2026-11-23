// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <tuple>
#include <type_traits>

#include "infra/byte_count.h"
#include "infra/message_sum.h"
#include "infra/seat.h"
#include "os/raw_monotonic_clock.h"
#include "proto/disk_address.h"
#include "proto/mailbox_bus.h"
#include "proto/mailbox_run.h"
#include "proto/mailbox_words.h"
#include "proto/aux_value.h"
#include "proto/types.h"
#include "proto/uio_opcodes.h"

namespace mister::proto {

inline constexpr std::size_t kMailboxCommandWords = 3;

inline constexpr FioWordCount kMailboxDirectWords{64};

struct MailboxActLen {
    std::uint16_t v = 0;

    [[nodiscard]] static constexpr MailboxActLen whole() noexcept {
        return MailboxActLen{static_cast<std::uint16_t>(kMailboxActBytes)};
    }

    [[nodiscard]] static constexpr MailboxActLen clamped(infra::ByteCount bytes) noexcept {
        return MailboxActLen{
            static_cast<std::uint16_t>(bytes.v < kMailboxActBytes ? bytes.v : kMailboxActBytes)
        };
    }

    template <class Byte>
    [[nodiscard]] constexpr std::span<Byte> of(
        std::span<Byte, kMailboxActBytes> buffer
    ) const noexcept {
        return std::span<Byte>(buffer).first(std::min<std::size_t>(v, buffer.size()));
    }

    friend constexpr bool operator==(MailboxActLen, MailboxActLen) = default;
    friend constexpr auto operator<=>(MailboxActLen, MailboxActLen) = default;
};

struct MailboxAct {
    TASTY_SEAT_EXEMPT(component);
    enum class Kind : std::uint8_t {
        None,
        Command,
        Download,
        CommandRun,
        AddressedDownload,
        AddressedUpload,
        BusWrite,
        BusRead,
        kCount,
    };

    using NotBefore = os::RawMonotonicClock::time_point;

    struct None {
        static constexpr Kind kKind = Kind::None;
    };

    struct Command {
        static constexpr Kind kKind = Kind::Command;
        UioOp opcode{};
        MailboxWords words{};
    };

    struct Download {
        static constexpr Kind kKind = Kind::Download;
        WideIoIndex index{};
        MailboxActLen len{};
    };

    struct CommandRun {
        static constexpr Kind kKind = Kind::CommandRun;
        NotBefore not_before{};
        ReplyCount commands{};
        MailboxRunGuard guard = MailboxRunGuard::kNone;
        MailboxRunWords words = MailboxRunWords::One;
        std::array<std::byte, 5> pad_{};
    };

    struct AddressedDownload {
        static constexpr Kind kKind = Kind::AddressedDownload;
        NotBefore not_before{};
        AuxValue address{};
        WideIoIndex index{};
        MailboxActLen len{};
    };

    struct AddressedUpload {
        static constexpr Kind kKind = Kind::AddressedUpload;
        NotBefore not_before{};
        AuxValue address{};
        WideIoIndex index{};
        MailboxActLen len{};
    };

    struct BusWrite {
        static constexpr Kind kKind = Kind::BusWrite;
        NotBefore not_before{};
        DiskAddress address{};
        UioOp opcode{};
        std::array<std::byte, 2> pad_{};
        MailboxBusLen len{};
    };

    struct BusRead {
        static constexpr Kind kKind = Kind::BusRead;
        NotBefore not_before{};
        DiskAddress address{};
        UioOp opcode{};
        std::array<std::byte, 2> pad_{};
        MailboxBusLen len{};
    };

    static_assert(sizeof(BusWrite) == 24, "not_before@0 address@8 opcode@12 pad_@14 len@16");
    static constexpr std::size_t kStore = sizeof(BusWrite);
    static_assert(
        sizeof(BusWrite) >= sizeof(CommandRun)
            && sizeof(BusWrite) == sizeof(BusRead)
            && sizeof(CommandRun) >= sizeof(Command)
            && sizeof(CommandRun) >= sizeof(Download)
            && sizeof(CommandRun) == sizeof(AddressedDownload)
            && sizeof(CommandRun) == sizeof(AddressedUpload),
        "the store holds every alternative"
    );

    using Alternatives = std::tuple<
        None,
        Command,
        Download,
        CommandRun,
        AddressedDownload,
        AddressedUpload,
        BusWrite,
        BusRead>;

    Kind kind = Kind::None;
    std::array<std::byte, 7> pad_{};
    alignas(alignof(BusWrite)) std::array<std::byte, kStore> store{};
};

static_assert(infra::MessageSum<MailboxAct> && infra::alternatives_are_total<MailboxAct>());
static_assert(std::is_trivially_copyable_v<MailboxAct>);

}
