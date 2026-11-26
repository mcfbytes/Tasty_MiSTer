// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "infra/indexed_array.h"
#include "infra/opt_ref.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "proto/block_geometry.h"
#include "proto/save_slot_rule.h"
#include "proto/types.h"
#include "svc/save_files_on_io.h"
#include "svc/types.h"

namespace mister::app {

class SaveSlotBlocks {
    TASTY_SEAT_RESIDENT(Io);

public:
    enum class Read : std::uint8_t { File, Blank };

    enum class Write : std::uint8_t { Wrote, Created, Discarded, Failed, NoFile };

    explicit SaveSlotBlocks(infra::OptRef<const svc::ISaveFilesOnIo> saves) noexcept
        : saves_(saves) {}

    SaveSlotBlocks(const SaveSlotBlocks&) = delete;
    SaveSlotBlocks& operator=(const SaveSlotBlocks&) = delete;

    [[nodiscard]] bool serves() const noexcept { return saves_.has_value(); }

    [[nodiscard]] Read read_block(
        proto::SlotIndex slot,
        proto::BlockSize bytes,
        proto::Lba lba,
        std::span<std::byte> out
    ) noexcept;

    [[nodiscard]] Write write_block(
        proto::SlotIndex slot,
        proto::BlockSize bytes,
        proto::Lba lba,
        std::span<const std::byte> block
    ) noexcept;

private:
    struct SaveState {
        std::optional<svc::SaveServeGen> gen{};
        proto::SaveSlotRule rule{};
    };

    [[nodiscard]] svc::ISaveFilesOnIo::Save save_of_(proto::SlotIndex slot) noexcept;

    infra::OptRef<const svc::ISaveFilesOnIo> saves_;

    infra::IndexedArray<SaveState, proto::kBlockSlots, proto::SlotIndex> rules_{};
};

}
