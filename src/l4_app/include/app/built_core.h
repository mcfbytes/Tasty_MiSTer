// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>

#include "app/core_deleter.h"
#include "app/core_window.h"
#include "app/ocram_frame_grant.h"
#include "app/osd_grant.h"
#include "app/session_routes.h"
#include "app/sys_grant.h"
#include "cores/core_window_grant.h"
#include "cores/disk_servant.h"
#include "hal/fpga_memory.h"
#include "infra/indexed_array.h"
#include "infra/prelude.h"
#include "proto/block_geometry.h"
#include "proto/build_generation.h"
#include "proto/types.h"
#include "svc/storage_backend.h"
#include "infra/seat.h"
#include "infra/xthread/slot_pool.h"

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

    std::unique_ptr<svc::IStorageBackend> save_store;

    cores::DiskServantSet disk_servants{};

    infra::IndexedArray<
        std::unique_ptr<svc::IStorageBackend>,
        proto::kAnnounceableSlots,
        proto::SlotIndex>
        slot_servants{};
    CorePtr core;
    std::array<std::optional<CoreWindow>, cores::kMaxCoreWindows> windows{};
    std::optional<hal::FpgaMemory> fed_ring{};
    SessionRoutes routes{};

    OcramFrameGrant frame_lane{};
    proto::BuildGeneration gen{};
};

using CoreBay = xthread::SlotPool<BuiltCore, kCoreBaySlots, SeatTag::Main>;

}
