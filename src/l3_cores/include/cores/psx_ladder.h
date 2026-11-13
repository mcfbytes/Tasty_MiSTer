// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include "cores/boot_ladder.h"
#include "cores/core_profile.h"
#include "cores/ladder_host.h"
#include "cores/row_cursor.h"
#include "infra/error.h"
#include "os/clock.h"
#include "svc/vfs.h"

namespace mister::cores {

class PsxLadder final : public BootLadder {
    TASTY_SEAT_RESIDENT(Main);

public:
    PsxLadder(const CoreProfile& profile, const LadderContext& ctx)
        : BootLadder(profile, ctx),
          row_(profile.boot_assets),
          last_dir_(ctx.last_dir),
          noreset_(ctx.noreset) {}

    [[nodiscard]] bool reset_requested() const noexcept { return facts_.reset; }

    [[nodiscard]] std::uint16_t libcrypt_mask() const noexcept { return facts_.libcrypt_mask; }

private:
    [[nodiscard]] LadderPc do_pc() const noexcept override {
        TASTY_SEAT_BODY(PsxLadder);
        return LadderPc::of(rung_);
    }

    [[nodiscard]] LadderCarry do_carry() const noexcept override {
        TASTY_SEAT_BODY(PsxLadder);
        return LadderCarry{.last_dir = last_dir_, .noreset = noreset_};
    }

    enum class Rung : std::uint8_t {
        MountDisc = 0,
        Bios = 1,
        MemCard = 2,
        DiscPayload = 3,
        Announce = 4,
        Eject = 10,
        Done = LadderPc::kDone,
    };

    [[nodiscard]] Ex<bool> on_step() override;

    [[nodiscard]] WideIoIndex menu_file_index() const;

    void decide_stage_edge_();
    [[nodiscard]] std::uint16_t scan_libcrypt_() const;

    RowCursor row_;
    Rung rung_ = Rung::MountDisc;
    Half half_ = Half::Fresh;

    std::string last_dir_;
    bool noreset_ = false;
    bool stage_save_ = false;
    bool stage_assets_ = false;
    proto::LinkOp::StageDiscPayload facts_{};
    proto::ActKey ss_gen_{};
    std::chrono::nanoseconds ss_due_ns_{};
};

}
