// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>

#include "cores/loader.h"
#include "infra/seat.h"

namespace mister::cores {

class MsuDataLoader final : public ILoader {
    TASTY_SEAT_RESIDENT(Main);

public:
    [[nodiscard]] Ex<LoadPlan> plan(const LoadAsk& ask, const proto::StatusWord& status) override;
    void shape(const TransferRow&, std::span<std::uint8_t>, std::uint64_t) override {
        TASTY_SEAT_BODY(MsuDataLoader);
    }
    [[nodiscard]] Ex<void> place(const TransferRow& row, std::span<const std::uint8_t> piece,
                                 std::uint64_t off, ICoreWindow& window) override;
    [[nodiscard]] Ex<void> finish(ICoreWindow*) override {
        TASTY_SEAT_BODY(MsuDataLoader);
        return {};
    }
    [[nodiscard]] std::span<const std::uint8_t> facts() const noexcept override {
        TASTY_SEAT_BODY(MsuDataLoader);
        return {};
    }
};

}  // namespace mister::cores
