// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "cores/boot_ladder.h"
#include "cores/core_profile.h"
#include "cores/ladder_carry.h"
#include "cores/ladder_context.h"
#include "cores/minimig/kickstart.h"
#include "cores/minimig/minimig_config.h"
#include "cores/minimig/minimig_ladder_frames.h"
#include "cores/minimig/minimig_ladder_screen.h"
#include "cores/payload_pieces.h"
#include "infra/error.h"
#include "infra/indexed_array.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "infra/tally.h"
#include "os/deadline.h"
#include "proto/types.h"

namespace mister::cores::minimig {

class MinimigLadder final : public BootLadder {
    TASTY_SEAT_RESIDENT(Main);

public:
    MinimigLadder(const CoreProfile& profile, const LadderContext& ctx);

    static constexpr std::chrono::milliseconds kVideoSettle{100};
    static constexpr std::chrono::milliseconds kScreenHold{500};

    [[nodiscard]] infra::Tally steps_refused() const noexcept { return steps_refused_; }

    [[nodiscard]] bool halted() const noexcept { return halted_; }

private:
    [[nodiscard]] LadderPc do_pc() const noexcept override {
        TASTY_SEAT_BODY(MinimigLadder);
        return LadderPc::of(rung_);
    }

    [[nodiscard]] LadderCarry do_carry() const noexcept override {
        TASTY_SEAT_BODY(MinimigLadder);
        return LadderCarry{
            .last_dir = image_dir(),
            .slot_paths = std::span<const std::string>(paths_.begin(), paths_.size())
        };
    }

    enum class Rung : std::uint8_t {
        Bind = 0,
        Sample = 1,
        Restore = 2,
        Prologue = 3,
        Draw = 4,
        Credits = 5,
        Report = 6,
        Machine = 7,
        Hrtmon = 8,
        Kickstart = 9,
        Release = 10,
        WriteBack = 11,
        Nic = 12,
        Floppy = 13,
        Done = LadderPc::kDone,
    };

    [[nodiscard]] Ex<bool> on_step() override;

    [[nodiscard]] Ex<bool> drain_();

    void hold_after_drain_(std::chrono::milliseconds hold) noexcept { hold_ = hold; }

    [[nodiscard]] Ex<bool> bind_pick_();
    [[nodiscard]] Ex<bool> bind_floppy_();
    [[nodiscard]] Ex<bool> sample_live_();
    void restore_();

    void prologue_();
    void draw_();
    void credits_();

    void announce_();

    void machine_();
    void hrtmon_();
    void kickstart_();
    void release_();
    [[nodiscard]] Ex<bool> write_back_();

    [[nodiscard]] bool start_nic_();

    [[nodiscard]] bool upload_kickstart_(std::string_view path);

    [[nodiscard]] bool upload_with_ext_rom_();
    void queue_(const Kickstart& rom);

    [[nodiscard]] Ex<void> detach_floppies_();
    [[nodiscard]] Ex<void> reset_hard_disks_();
    [[nodiscard]] Ex<MinimigConfig::IdeUnits> open_hard_disks_();
    [[nodiscard]] Ex<void> open_cd_drives_();

    template <class T>
    void count_refusal_(const Ex<T>& step) noexcept {
        if (!step)
            steps_refused_.add();
    }

    [[nodiscard]] std::vector<std::byte> read_(std::string_view path, proto::FileSize cap) const;

    [[nodiscard]] std::vector<std::byte> read_home_(
        std::string_view name,
        proto::FileSize cap
    ) const;

    Rung rung_ = Rung::Done;
    MinimigConfig cfg_{};
    std::string config_;
    proto::IoIndex slot_{};
    std::string picked_;
    bool boots_ = false;
    bool reload_ = false;
    bool defaulted_ = false;
    bool halted_ = false;
    bool starts_nic_ = false;
    bool renamed_ = false;
    infra::Tally steps_refused_{};

    FrameQueue frames_{};
    BootScreen screen_{};
    std::optional<PayloadPieces> run_{};
    proto::ActKey16 act_{};
    os::Deadline answer_due_{os::Deadline::immediate()};
    std::optional<std::chrono::milliseconds> hold_{};
    std::optional<os::Deadline> held_until_{};

    infra::IndexedArray<std::string, kLadderCarrySlots, proto::IoIndex> paths_{};
};

}
