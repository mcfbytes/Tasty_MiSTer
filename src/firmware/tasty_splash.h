// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>

#include "hal/fpga_memory.h"
#include "infra/error.h"
#include "infra/seat.h"

namespace mister::app {
class VideoPump;
}

namespace mister::fw {

class TastySplash {
    TASTY_SEAT_RESIDENT(Ui);

public:
    static constexpr std::uint32_t kDefaultW = 1280;
    static constexpr std::uint32_t kDefaultH = 720;
    static constexpr std::size_t kPixelOff = 4096;
    static constexpr std::uint16_t kFbEnWord = 0x8016;
    static constexpr std::int64_t kShowNs = 2'000'000'000;

    [[nodiscard]] Ex<void> map(const hal::PhysRegion& fb) noexcept;
    [[nodiscard]] bool blit(std::uint32_t width, std::uint32_t height) noexcept;
    [[nodiscard]] Ex<void> show(app::VideoPump& video, std::uint32_t width,
                                std::uint32_t height) noexcept;
    [[nodiscard]] Ex<void> hide(app::VideoPump& video) noexcept;
    [[nodiscard]] bool mapped() const noexcept { return fb_.has_value(); }
    [[nodiscard]] static std::uint32_t pixel(std::uint32_t x, std::uint32_t y) noexcept;

private:
    std::optional<hal::FpgaMemory> fb_{};
    hal::PhysRegion fb_region_{};
};

}  // namespace mister::fw
