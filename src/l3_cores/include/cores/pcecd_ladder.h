// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <memory>
#include <string_view>

#include "cores/boot_asset.h"
#include "cores/boot_ladder.h"
#include "cores/core_profile.h"
#include "cores/ladder_host.h"
#include "cores/row_cursor.h"
#include "infra/error.h"
#include "os/clock.h"
#include "svc/vfs.h"

namespace mister::cores {

class PceCdLadder final : public BootLadder {
    TASTY_SEAT_RESIDENT(Main);

public:
    PceCdLadder(const CoreProfile& profile, const LadderContext& ctx)
        : BootLadder(profile, ctx),
          row_(profile.boot_assets) {}

private:
    [[nodiscard]] LadderPc do_pc() const noexcept override {
        TASTY_SEAT_BODY(PceCdLadder);
        return LadderPc::of(rung_);
    }
    enum class Rung : std::uint8_t {
        MountDisc = 0,
        SgxProbe = 1,
        Bios = 2,
        NotifyMount = 3,
        Eject = 10,
        PulseRelease = 11,
        Done = LadderPc::kDone,
    };

    static constexpr std::string_view kSgxSibling = "sgx";
    static constexpr WideIoIndex kSgxDest = WideIoIndex::of(IoIndex{1}, 0);
    static_assert(kSgxDest == WideIoIndex{1});
    bool sgx_seen_ = false;

    [[nodiscard]] WideIoIndex bios_dest_(const BootAsset& row) const noexcept {
        return sgx_seen_ ? kSgxDest : row.dest;
    }

    [[nodiscard]] Ex<bool> on_step() override;

    [[nodiscard]] Ex<AssetWait> load_pce_bios(const BootAsset& row);
    [[nodiscard]] Ex<AssetWait> finish_pce_bios_();

    RowCursor row_;
    Rung rung_ = Rung::MountDisc;
    Half half_ = Half::Fresh;

    proto::RegionCode pce_region_{};
};

}
