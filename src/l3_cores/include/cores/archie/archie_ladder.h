// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "cores/archie/archie_core.h"
#include "cores/boot_asset.h"
#include "cores/boot_ladder.h"
#include "cores/ladder_carry.h"
#include "cores/ladder_context.h"
#include "cores/ladder_pc.h"
#include "cores/payload_pieces.h"
#include "infra/error.h"
#include "infra/indexed_array.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "proto/types.h"

namespace mister::cores::archie {

class ArchieLadder final : public BootLadder {
    TASTY_SEAT_RESIDENT(Main);

public:
    ArchieLadder(const CoreProfile& profile, const LadderContext& ctx);

    static constexpr BootAsset kDefaultRom{
        .name = "RISCOS.ROM",
        .where = kCoreHomeDir,
        .subdir = "ARCHIE",
        .dest = proto::WideIoIndex::of(ArchieCore::kRomIndex)
    };

    static constexpr std::string_view kCmosSaved = "config/nvram/cmos.dat";
    static constexpr BootAsset kCmosStock{
        .name = ArchieCore::kCmosFile,
        .where = kCoreHomeDir,
        .subdir = "ARCHIE"
    };

    static constexpr proto::FioWordCount kCmosPieceWords{256};

private:
    [[nodiscard]] LadderPc do_pc() const noexcept override {
        TASTY_SEAT_BODY(ArchieLadder);
        return LadderPc::of(rung_);
    }

    [[nodiscard]] LadderCarry do_carry() const noexcept override {
        TASTY_SEAT_BODY(ArchieLadder);
        return LadderCarry{
            .last_dir = image_dir(),
            .slot_paths = std::span<const std::string>(paths_.begin(), paths_.size())
        };
    }

    enum class Rung : std::uint8_t {
        Restore = 0,
        Hold = 1,
        Bind = 2,
        Rom = 3,
        Cmos = 4,
        Release = 5,
        Done = LadderPc::kDone,
    };

    [[nodiscard]] Ex<bool> on_step() override;

    void restore_();
    [[nodiscard]] Ex<bool> bind_rom_();
    [[nodiscard]] Ex<bool> rom_rung_();
    [[nodiscard]] Ex<bool> cmos_rung_();

    [[nodiscard]] std::string cmos_path_() const;
    [[nodiscard]] std::vector<std::uint8_t> read_cmos_() const;

    [[nodiscard]] std::string& rom_path_() noexcept {
        return infra::at_index(paths_, ArchieCore::kRomSlot.block_slot());
    }

    Rung rung_ = Rung::Done;
    bool boots_ = false;
    std::string config_;
    std::optional<PayloadPieces> cmos_{};
    bool cmos_read_ = false;

    std::array<std::string, kLadderCarrySlots> paths_{};
};

}
