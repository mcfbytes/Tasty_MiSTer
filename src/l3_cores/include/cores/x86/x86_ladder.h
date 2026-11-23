// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

#include "cores/boot_ladder.h"
#include "cores/ladder_carry.h"
#include "cores/ladder_context.h"
#include "cores/x86/bus_register_write.h"
#include "cores/x86/floppy_media.h"
#include "cores/x86/x86_cmos.h"
#include "cores/x86/x86_family.h"
#include "cores/x86/x86_slot.h"
#include "infra/indexed_array.h"
#include "infra/seat.h"
#include "os/clock.h"

namespace mister::cores::x86 {

class X86Ladder final : public BootLadder {
    TASTY_SEAT_RESIDENT(Main);

public:
    static constexpr std::chrono::milliseconds kEjectGap{100};

    static constexpr proto::IoIndex kShadowWalk{0};

    X86Ladder(const CoreProfile& profile, const LadderContext& ctx, const X86Family& family);

private:
    X86Ladder(const X86Ladder&) = delete;
    X86Ladder& operator=(const X86Ladder&) = delete;

    enum class Rung : std::uint8_t {
        Paths = 0,
        Hold = 1,
        Shadow = 2,
        ShadowWait = 3,
        Mount = 4,
        Eject = 5,
        Gap = 6,
        Insert = 7,
        Cmos = 8,
        Release = 9,
        BindPick = 10,
        IdeBinds = 11,
        Done = LadderPc::kDone,
    };

    [[nodiscard]] LadderPc do_pc() const noexcept override {
        TASTY_SEAT_BODY(X86Ladder);
        return LadderPc::of(rung_);
    }

    [[nodiscard]] LadderCarry do_carry() const noexcept override {
        TASTY_SEAT_BODY(X86Ladder);
        return LadderCarry{
            .last_dir = image_dir(),
            .slot_paths = std::span<const std::string>(paths_.begin(), paths_.size())
        };
    }

    [[nodiscard]] bool steps_at_core_start() const noexcept override {
        TASTY_SEAT_BODY(X86Ladder);
        return true;
    }

    [[nodiscard]] Ex<bool> on_step() override;

    void read_paths_();
    [[nodiscard]] Ex<bool> shadow_rung_();
    [[nodiscard]] Ex<bool> shadow_wait_();
    [[nodiscard]] Ex<bool> mount_rung_();
    [[nodiscard]] Ex<bool> gap_rung_();
    [[nodiscard]] Ex<bool> insert_rung_();
    [[nodiscard]] Ex<bool> cmos_rung_();
    [[nodiscard]] Ex<bool> bind_pick_();

    [[nodiscard]] Ex<bool> ide_binds_rung_();

    void next_drive_() noexcept;

    [[nodiscard]] FloppyMedia probe_(proto::DiskUnit drive, bool opened);
    [[nodiscard]] X86CmosInputs cmos_inputs_() const;

    [[nodiscard]] bool write_(const BusRegisterWrite& w);

    [[nodiscard]] std::string& path_(X86Slot slot) noexcept {
        return infra::at_index(paths_, slot);
    }

    const X86Family& family_;
    Rung rung_ = Rung::Done;
    bool whole_init_ = false;
    bool share_asked_ = false;
    bool reads_config_ = false;
    proto::DiskUnit drive_{};
    std::optional<X86Slot> pick_{};
    std::span<const X86Slot> ide_left_{};
    proto::ActKey walk_gen_{};
    infra::IndexedArray<FloppyMedia, X86Slot::kFloppies, proto::DiskUnit> media_{};
    FloppyMedia::Registers insert_{};
    std::span<const BusRegisterWrite> insert_left_{};
    std::optional<X86CmosImage> cmos_{};
    CmosRegister cmos_at_{};
    os::RawMonotonicClock::time_point gap_until_{};

    std::array<std::string, kLadderCarrySlots> paths_{};
};

}
