// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "app/block_grant.h"
#include "app/mount_slot.h"
#include "hal/aperture_region.h"
#include "hal/ocram.h"
#include "hal/sector_block.h"
#include "infra/opt_ref.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "infra/xthread/telemetry.h"
#include "os/uio_line_ref.h"
#include "proto/types.h"
#include "svc/disc/sector_program.h"

namespace mister::app {

struct SectorCard {
    hal::SectorBlock::Slot index{};
    proto::SlotIndex slot{};
    std::uint16_t frame_words = 0;
    std::optional<hal::ApertureRegion> image{};
};

struct SectorGrantBody {
    static constexpr std::size_t kCards = 2;
    os::UioLineRef line{};
    infra::OptRef<const svc::disc::ISectorProgram> program{};
    hal::ocram::Words frame_words{};
    std::array<SectorCard, kCards> cards{};
    hal::ocram::RingLend ring{};
    hal::ocram::RingLend evt{};

    hal::ocram::RingLend mount{};
    std::array<MountSlot, kCards> mounts{};
    bool bind_records = false;
    bool events = false;
    proto::BindGeneration conf_gen{};

    [[nodiscard]] bool live() const noexcept { return program.has_value(); }

    [[nodiscard]] bool on_mailbox() const noexcept { return ring.lent() && evt.lent(); }
};

static_assert(
    SectorGrantBody::kCards == hal::ocram::kMountSlots.size(),
    "the mailbox's mount slots are the block's two save slots, element i slot k = i + 1"
);

struct SectorGrant : BlockGrant<SectorGrantBody> {

    [[nodiscard]] bool live() const noexcept {
        return body.live() && (block.lent() || body.on_mailbox());
    }
};

static_assert(BlockGrantRecord<SectorGrant>);
using SectorGrantCell = xthread::Telemetry<SectorGrant, SeatTag::Main>;

}
