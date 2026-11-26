// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>

#include "app/lw_grant_stamp.h"
#include "app/mount_slot.h"
#include "app/sector_grant.h"
#include "hal/ocram.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "os/uio_handle.h"
#include "proto/types.h"
#include "svc/disc/sector_program.h"

namespace mister::app {

struct OcramSector {
    TASTY_SEAT_EXEMPT(component);
    using Grant = SectorGrant;
    hal::ocram::RingLend ring{};
    hal::ocram::RingLend evt{};
    os::UioHandle line{};
    infra::OptRef<const svc::disc::ISectorProgram> program{};
    hal::ocram::Words frame{};
    hal::ocram::RingLend mount{};
    std::array<MountSlot, SectorGrantBody::kCards> mounts{};
    bool bind_records = false;
    bool events = false;
    proto::BindGeneration conf_gen{};
    LwGrantStamp stamp{};

    [[nodiscard]] proto::BlockSlotMask mount_slot_mask() const noexcept {
        proto::BlockSlotMask m{};
        for (const MountSlot& c : mounts)
            if (c.granted())
                m = m.with(c.slot);
        return m;
    }

    [[nodiscard]] proto::BlockSlotMask bridge_slots() const noexcept {
        const proto::BlockSlotMask mounted = mount_slot_mask();
        if (mounted.empty() || !program)
            return proto::BlockSlotMask{};
        return mounted.with(program->slot());
    }

    [[nodiscard]] Grant lend() const noexcept {
        return Grant{
            {.block = {},
             .gen = stamp.gen(),
             .body = {
                 .line = line.borrow(),
                 .program = program,
                 .frame_words = frame,
                 .ring = ring,
                 .evt = evt,
                 .mount = mount,
                 .mounts = mounts,
                 .bind_records = bind_records,
                 .events = events,
                 .conf_gen = conf_gen
             }}
        };
    }
};

}
