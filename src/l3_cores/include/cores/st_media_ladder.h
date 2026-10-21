// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "cores/boot_ladder.h"
#include "cores/core_profile.h"
#include "cores/ladder_context.h"
#include "cores/st_media_fact.h"
#include "infra/error.h"
#include "svc/file.h"

namespace mister::cores {

class StMediaLadder final : public BootLadder {
    TASTY_SEAT_RESIDENT(Main);

public:
    StMediaLadder(const CoreProfile& profile, const LadderContext& ctx);

    [[nodiscard]] std::uint8_t pc() const noexcept override {
        TASTY_SEAT_BODY(StMediaLadder);
        return static_cast<std::uint8_t>(rung_);
    }

    [[nodiscard]] std::span<const std::string> next_slot_paths() const noexcept override {
        TASTY_SEAT_BODY(StMediaLadder);
        return paths_;
    }

    static constexpr std::uint64_t kTosReadCap = 1024u * 1024u;

    [[nodiscard]] static std::vector<std::uint8_t> read_whole(svc::IFile& f, std::uint64_t cap);

private:
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
        Done = kDone,
    };

    [[nodiscard]] Ex<bool> on_step() override;

    void restore_();
    [[nodiscard]] bool order_fact_(StMediaFact::Verb verb, Rung next);
    [[nodiscard]] Ex<bool> order_mount_(std::uint8_t slot, std::string_view path, Rung next);
    [[nodiscard]] Ex<bool> bind_acsi_(std::uint8_t slot, std::string_view path, Rung next);

    [[nodiscard]] std::vector<std::uint8_t> read_tos_() const;
    [[nodiscard]] std::vector<std::uint8_t> read_cart_() const;
    [[nodiscard]] Ex<bool> intern_(std::uint8_t index, std::vector<std::uint8_t>&& bytes);
    [[nodiscard]] Ex<bool> order_file_(std::uint8_t index, Rung answer);
    [[nodiscard]] Ex<bool> await_file_(Rung next);

    Rung rung_ = Rung::Done;
    bool cold_ = false;
    std::uint8_t slot_ = 0;
    proto::FileId file_{};
    std::uint32_t act_ = 0;
    std::int64_t due_ns_ = 0;
    std::array<std::string, kLadderCarrySlots> paths_{};
};

}
