// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "cores/ide/ide_channel_row.h"
#include "cores/ide/ide_types.h"
#include "proto/types.h"

namespace mister::cores::x86 {

struct IdeUnit {

    struct Channel {
        std::uint8_t v = 0;

        [[nodiscard]] constexpr bool declared_in(
            std::span<const ide::IdeChannelRow> channels
        ) const noexcept {
            return v < channels.size();
        }

        friend constexpr bool operator==(Channel, Channel) = default;
    };

    Channel channel{};
    ide::IdeDriveSelect drive = ide::IdeDriveSelect::Master;
    friend constexpr bool operator==(IdeUnit, IdeUnit) = default;
};

struct X86Slot {
    std::uint8_t v = 0;

    static constexpr std::size_t kCount = 6;
    static constexpr std::size_t kFloppies = 2;

    [[nodiscard]] static constexpr std::optional<X86Slot> of(proto::IoIndex io) noexcept {
        if (io.v >= kCount)
            return std::nullopt;
        return X86Slot{io.v};
    }

    [[nodiscard]] static constexpr std::optional<X86Slot> of(proto::DiskUnit drive) noexcept {
        if (drive.v >= kCount)
            return std::nullopt;
        return X86Slot{drive.v};
    }

    [[nodiscard]] constexpr std::optional<proto::DiskUnit> floppy() const noexcept {
        if (v >= kFloppies)
            return std::nullopt;
        return proto::DiskUnit{v};
    }

    [[nodiscard]] constexpr std::optional<IdeUnit> ide() const noexcept {
        if (v < kFloppies)
            return std::nullopt;
        constexpr std::uint8_t kDrivesPerChannel = 2;
        const std::uint8_t past = static_cast<std::uint8_t>(v - kFloppies);
        return IdeUnit{
            IdeUnit::Channel{static_cast<std::uint8_t>(past / kDrivesPerChannel)},
            past % kDrivesPerChannel == 0 ? ide::IdeDriveSelect::Master : ide::IdeDriveSelect::Slave
        };
    }

    [[nodiscard]] constexpr proto::IoIndex io_index() const noexcept { return proto::IoIndex{v}; }

    [[nodiscard]] constexpr proto::SlotIndex block_slot() const noexcept {
        return proto::SlotIndex{v};
    }

    friend constexpr bool operator==(X86Slot, X86Slot) = default;
};

inline constexpr X86Slot kFloppyA{0};
inline constexpr X86Slot kFloppyB{1};
inline constexpr X86Slot kIde00{2};
inline constexpr X86Slot kIde01{3};
inline constexpr X86Slot kIde10{4};
inline constexpr X86Slot kIde11{5};

inline constexpr std::array kX86Slots{kFloppyA, kFloppyB, kIde00, kIde01, kIde10, kIde11};

static_assert(!kFloppyA.ide() && !kFloppyB.ide());
static_assert(kIde00.ide() == IdeUnit{IdeUnit::Channel{0}, ide::IdeDriveSelect::Master});
static_assert(kIde01.ide() == IdeUnit{IdeUnit::Channel{0}, ide::IdeDriveSelect::Slave});
static_assert(kIde10.ide() == IdeUnit{IdeUnit::Channel{1}, ide::IdeDriveSelect::Master});
static_assert(kIde11.ide() == IdeUnit{IdeUnit::Channel{1}, ide::IdeDriveSelect::Slave});

}
