// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>

#include "app/block_poll_route.h"
#include "app/core_deleter.h"
#include "app/core_window.h"
#include "app/doorbell_lines.h"
#include "app/lw_pads.h"
#include "app/osd_grant.h"
#include "app/sys_grant.h"
#include "cores/core_window_grant.h"
#include "hal/fpga_memory.h"
#include "proto/types.h"
#include "infra/seat.h"
#include "infra/slot_pool.h"

namespace mister::app {

inline constexpr std::size_t kMaxAbandonedCores = 4;

inline constexpr std::size_t kCoreBaySlots = kMaxAbandonedCores + 2;

inline constexpr std::array<proto::CoreBaySlot, kCoreBaySlots> kAllBaySlots =
    []<std::size_t... I>(std::index_sequence<I...>) {
        return std::array<proto::CoreBaySlot, kCoreBaySlots>{proto::CoreBaySlot{I}...};
    }(std::make_index_sequence<kCoreBaySlots>{});

[[nodiscard]] constexpr bool in_bay(proto::CoreBaySlot slot) noexcept {
    return slot.within<kCoreBaySlots>();
}

struct BuiltCore {
    TASTY_SEAT_MEDIATOR(Main, RT);
    CorePtr core;
    std::array<std::optional<CoreWindow>, cores::kMaxCoreWindows> windows{};
    std::optional<hal::FpgaMemory> fed_ring{};
    DoorbellLines lines{};
    std::optional<LwPads> pads{};
    OsdPageRoute osd_pages = OsdPageRoute::Link;
    proto::BlockSlotMask lw_slots{};
    StatusRoute status_route = StatusRoute::Link;
    BlockPollRoute block_poll = BlockPollRoute::Link;
    std::uint16_t gen = 0;
};

using CoreBay = xthread::SlotPool<BuiltCore, kCoreBaySlots, SeatTag::Main>;

}
