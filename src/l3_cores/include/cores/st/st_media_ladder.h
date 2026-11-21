// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "cores/boot_ladder.h"
#include "cores/core_profile.h"
#include "cores/ladder_context.h"
#include "cores/load_plan.h"
#include "cores/st/st_media_fact.h"
#include "cores/st/st_slot.h"
#include "infra/error.h"
#include "os/deadline.h"
#include "infra/prelude.h"
#include "infra/indexed_array.h"
#include "svc/file.h"

namespace mister::cores::st {

class StMediaLadder final : public BootLadder {
    TASTY_SEAT_RESIDENT(Main);

public:
    StMediaLadder(const CoreProfile& profile, const LadderContext& ctx);

    static constexpr std::uint64_t kTosReadCap = 1024u * 1024u;

    [[nodiscard]] static std::vector<std::uint8_t> read_whole(svc::IFile& f, std::uint64_t cap);

private:
    [[nodiscard]] LadderPc do_pc() const noexcept override {
        TASTY_SEAT_BODY(StMediaLadder);
        return LadderPc::of(rung_);
    }

    [[nodiscard]] LadderCarry do_carry() const noexcept override {
        TASTY_SEAT_BODY(StMediaLadder);
        return LadderCarry{
            .last_dir = image_dir(),
            .slot_paths = std::span<const std::string>(paths_.begin(), paths_.size())
        };
    }

    enum class Rung : std::uint8_t {
        Bind = 0,
        Restore = 1,
        EjectA = 2,
        EjectB = 3,
        Hold = 4,
        Tos = 5,
        TosAnswer = 6,
        Acsi0 = 7,
        Acsi1 = 8,
        Cart = 9,
        CartAnswer = 10,
        Release = 11,
        Clear = 12,
        Done = LadderPc::kDone,
    };

    [[nodiscard]] Ex<bool> on_step() override;

    void restore_();
    [[nodiscard]] bool order_fact_(StMediaFact::Verb verb, Rung next);
    [[nodiscard]] Ex<bool> order_mount_(st_ctrl::StSlot slot, std::string_view path, Rung next);
    [[nodiscard]] Ex<bool> bind_acsi_(st_ctrl::StSlot slot, std::string_view path, Rung next);

    [[nodiscard]] std::vector<std::uint8_t> read_tos_() const;
    [[nodiscard]] std::vector<std::uint8_t> read_cart_() const;

    [[nodiscard]] bool dongle_now_();

    [[nodiscard]] Ex<LoadPlan> plan_media_(st_ctrl::StSlot slot, proto::FileSize size);

    [[nodiscard]] Ex<bool> start_run_(
        const TransferRow& row,
        std::span<const std::uint8_t> bytes,
        bool held
    );
    [[nodiscard]] Ex<bool> step_run_(Rung next);

    [[nodiscard]] Ex<bool> await_run_(Rung next);
    [[nodiscard]] Ex<bool> tos_rung_();
    [[nodiscard]] Ex<bool> cart_rung_();

    [[nodiscard]] Ex<bool> start_clear_();

    Rung rung_ = Rung::Done;
    bool cold_ = false;
    bool dongle_ = false;
    std::optional<bool> restored_dongle_{};
    std::string config_;
    st_ctrl::StSlot slot_{};
    proto::ActKey16 act_{};
    std::optional<PayloadPieces> pieces_{};
    std::optional<PayloadPieces> clear_{};
    os::Deadline due_{os::Deadline::immediate()};

    infra::IndexedArray<std::string, st_ctrl::StSlot::kCount, st_ctrl::StSlot> paths_{};
};

}
