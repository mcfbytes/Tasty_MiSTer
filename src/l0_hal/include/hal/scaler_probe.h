// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "hal/scaler_buffers.h"
#include "hal/scaler_header.h"
#include "infra/seat.h"

namespace mister::hal {

class ScalerProbe {
    TASTY_SEAT_EXEMPT(component);

public:
    using BufMask = std::array<bool, ScalerBuffers::kBuffers>;
    static_assert(ScalerBuffers::kBuffers == 3, "kAllLive spells out every buffer");
    static constexpr BufMask kAllLive{true, true, true};
    static constexpr std::int64_t kProbeNs = 250'000'000;

    struct Pick {
        std::size_t buf = 0;
        std::uint8_t lag = 0;
        bool woven = false;
        std::size_t prev = ScalerBuffers::kBuffers;
    };

    enum class Verdict : std::uint8_t { Pending, Large, LargeLowlat, Small, Dead };

    void restart(std::int64_t now) noexcept;
    void read(const ScalerBuffers& window) noexcept;
    [[nodiscard]] Verdict decide(std::int64_t now) noexcept;
    [[nodiscard]] BufMask moved(std::size_t stride) const noexcept;

    [[nodiscard]] static std::optional<Pick> pick_complete(
        const std::array<ScalerHeader, ScalerBuffers::kBuffers>& h,
        const BufMask& live = kAllLive) noexcept;

    [[nodiscard]] static bool port_stuck(ScalerBuffers& window) noexcept;

private:
    struct ProbeSide {
        std::array<std::uint8_t, ScalerBuffers::kBuffers> first{};
        BufMask seen{};
        BufMask moved{};
        BufMask triple{};
    };

    std::array<ProbeSide, 2> sides_{};
    std::int64_t start_ns_ = 0;
};

}  // namespace mister::hal
