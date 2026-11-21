// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>
#include <cstdint>
#include <optional>

#include "hal/fpga_memory.h"
#include "infra/error.h"
#include "infra/prelude.h"
#include "infra/seat.h"

namespace mister::app {
class VideoPump;
struct OutputSize;
}

namespace mister::fw {

class TastySplash {
    TASTY_SEAT_RESIDENT(Ui);

public:
    static constexpr std::uint32_t kDefaultW = 1280;
    static constexpr std::uint32_t kDefaultH = 720;
    static constexpr hal::SpiWord kFbEnWord{0x8016};
    static constexpr std::chrono::nanoseconds kShowNs{std::chrono::seconds{2}};

    [[nodiscard]] Ex<void> map(const hal::ApertureRegion& fb) noexcept;
    [[nodiscard]] bool blit(app::OutputSize out) noexcept;
    [[nodiscard]] Ex<void> show(app::VideoPump& video, app::OutputSize out) noexcept;
    [[nodiscard]] Ex<void> hide(app::VideoPump& video) noexcept;

    [[nodiscard]] bool mapped() const noexcept { return fb_.has_value(); }

    [[nodiscard]] static std::uint32_t pixel(std::uint32_t x, std::uint32_t y) noexcept;

private:
    std::optional<hal::FpgaMemory> fb_{};
    std::optional<hal::ApertureRegion> fb_region_{};
};

}
