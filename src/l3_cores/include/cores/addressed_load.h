// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <span>

#include "cores/load_plan.h"
#include "cores/loader.h"
#include "cores/loader_context.h"
#include "cores/transfer_row.h"
#include "hal/phys_region.h"
#include "infra/error.h"
#include "infra/seat.h"
#include "proto/status_word.h"
#include "proto/types.h"

namespace mister::cores {

class AddressedLoad final : public ILoader {
    TASTY_SEAT_RESIDENT(Main);

public:
    explicit AddressedLoad(const hal::PhysRegion& aperture) noexcept : aperture_(aperture) {}

    [[nodiscard]] static std::optional<TransferRow> window_row(
        std::uint32_t addr,
        std::uint64_t bytes,
        proto::WideIoIndex index,
        const hal::PhysRegion& aperture
    ) noexcept;

    [[nodiscard]] Ex<LoadPlan> plan(const LoadAsk& ask, const proto::StatusWord& status) override;
    void shape(const TransferRow& row, std::span<std::uint8_t> piece, std::uint64_t off) override;
    [[nodiscard]] Ex<void> finish(ICoreWindow* window) override;

    [[nodiscard]] std::span<const std::uint8_t> facts() const noexcept override {
        TASTY_SEAT_BODY(AddressedLoad);
        return {};
    }

private:
    hal::PhysRegion aperture_;
};

std::unique_ptr<ILoader> make_addressed_load(const LoaderContext& ctx);

}
