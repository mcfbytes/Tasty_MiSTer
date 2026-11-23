// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>
#include <span>

#include "cores/ide/ide_channel_row.h"
#include "cores/ide/ide_geometry_rule.h"
#include "cores/x86/x86_slot.h"
#include "infra/seat.h"
#include "proto/disk_poll_program.h"
#include "proto/disk_protocol_row.h"
#include "proto/types.h"

namespace mister::cores::x86 {

enum class X86BootRoms : std::uint8_t { None, DdrShadow };

enum class X86Memory : std::uint8_t { StatusRamSize, Fixed16Mb };

struct X86IdeBus {
    std::span<const ide::IdeChannelRow> channels{};
    const ide::IGeometryRule& geometry;
};

struct X86Family {
    TASTY_SEAT_EXEMPT(const_shared);

    proto::DiskPollProgram poll;

    proto::DiskUnits drives{};

    std::optional<X86IdeBus> ide{};
    X86BootRoms roms = X86BootRoms::None;
    X86Memory memory = X86Memory::Fixed16Mb;

    [[nodiscard]] constexpr bool declares(X86Slot slot) const noexcept {
        const std::optional<IdeUnit> unit = slot.ide();
        return ide && unit && unit->channel.declared_in(ide->channels);
    }
};

}
