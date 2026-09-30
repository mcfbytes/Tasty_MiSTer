// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include "cores/boot_ladder.h"
#include "cores/core_profile.h"
#include "cores/ladder_host.h"
#include "infra/error.h"
#include "os/clock.h"
#include "svc/vfs.h"

namespace mister::cores {

class PsxLadder final : public BootLadder {
    TASTY_SEAT_EXEMPT(main);

public:
    PsxLadder(const CoreProfile& profile, const LadderContext& ctx)
        : BootLadder(profile, ctx), last_dir_(ctx.last_dir), noreset_(ctx.noreset) {}

    [[nodiscard]] std::uint8_t pc() const noexcept override {
        return static_cast<std::uint8_t>(rung_);
    }
    [[nodiscard]] std::string_view next_last_dir() const noexcept override { return last_dir_; }
    [[nodiscard]] bool next_noreset() const noexcept override { return noreset_; }

    [[nodiscard]] bool reset_requested() const noexcept { return facts_.reset; }
    [[nodiscard]] std::uint16_t libcrypt_mask() const noexcept { return facts_.libcrypt_mask; }

private:
    enum class Rung : std::uint8_t {
        MountDisc = 0,
        Bios = 1,
        MemCard = 2,
        DiscPayload = 3,
        Announce = 4,
        Eject = 10,
        Done = kDone,
    };

    [[nodiscard]] Ex<bool> on_step() override;

    [[nodiscard]] std::uint16_t menu_file_index() const;

    void decide_stage_edge_();
    [[nodiscard]] std::uint16_t scan_libcrypt_() const;

    Rung rung_ = Rung::MountDisc;

    std::string last_dir_;
    bool noreset_ = false;
    bool stage_save_ = false;
    bool stage_assets_ = false;
    proto::LinkOp::StageDiscPayload facts_{};
};

}  // namespace mister::cores
