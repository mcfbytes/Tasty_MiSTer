// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <memory>
#include <span>

#include "cores/loader.h"
#include "cores/snes/snes_rom.h"
#include "infra/prelude.h"
#include "infra/seat.h"

namespace mister::cores::snes {

class SnesLoader final : public ILoader {
    TASTY_SEAT_RESIDENT(Main);

public:
    [[nodiscard]] Ex<LoadPlan> plan(const LoadAsk& ask, const proto::StatusWord& status) override;
    [[nodiscard]] Ex<std::unique_ptr<svc::IFile>> source(
        const LoadAsk& ask,
        std::unique_ptr<svc::IFile> raw
    ) override;

    [[nodiscard]] Ex<void> finish(infra::OptRef<ICoreWindow>) override {
        TASTY_SEAT_BODY(SnesLoader);
        return {};
    }

    [[nodiscard]] std::span<const std::uint8_t> facts() const noexcept override {
        TASTY_SEAT_BODY(SnesLoader);
        return {};
    }

private:
    struct Analysis {
        HeaderAddr addr{};
        CartSniff sniff{};
    };

    [[nodiscard]] Analysis analyze_(std::span<const std::uint8_t> image) noexcept;
};

}
