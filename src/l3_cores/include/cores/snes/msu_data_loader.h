// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>

#include "cores/loader.h"
#include "infra/opt_ref.h"
#include "infra/prelude.h"
#include "infra/seat.h"

namespace mister::cores::snes {

class MsuDataLoader final : public ILoader {
    TASTY_SEAT_RESIDENT(Main);

public:
    [[nodiscard]] Ex<LoadPlan> plan(const LoadAsk& ask, const proto::StatusWord& status) override;

    void shape(const TransferRow&, std::span<std::uint8_t>, proto::FileOffset) override {
        TASTY_SEAT_BODY(MsuDataLoader);
    }

    [[nodiscard]] Ex<void> place(
        const TransferRow& row,
        std::span<const std::uint8_t> piece,
        proto::FileOffset at,
        ICoreWindow& window
    ) override;

    [[nodiscard]] Ex<void> finish(infra::OptRef<ICoreWindow>) override {
        TASTY_SEAT_BODY(MsuDataLoader);
        return {};
    }

    [[nodiscard]] std::span<const std::uint8_t> facts() const noexcept override {
        TASTY_SEAT_BODY(MsuDataLoader);
        return {};
    }
};

}
