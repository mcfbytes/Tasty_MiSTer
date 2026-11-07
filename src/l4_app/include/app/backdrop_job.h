// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <span>

#include "app/backdrop_sampler.h"
#include "app/backdrop_scaler.h"
#include "app/flip_buffer.h"
#include "app/hd_rect.h"
#include "app/px_plane.h"
#include "infra/seat.h"

namespace mister::app {

class BackdropJob {
    TASTY_SEAT_RESIDENT(HdOsd);

public:
    static constexpr std::int32_t kBandRows = 64;
    enum class Step : std::uint8_t { Idle, Scale, Copy };

    struct Canvas {
        PxPlane frame{};
        HdRect game{};
        HdRect picture{};
        std::array<BackdropScalerTarget, 2> targets{};
        HdRect glass{};
    };

    explicit BackdropJob(BackdropScaler& scaler) noexcept : scaler_(scaler) {}

    BackdropJob(const BackdropJob&) = delete;
    BackdropJob& operator=(const BackdropJob&) = delete;

    [[nodiscard]] Step step() const noexcept { return step_; }

    [[nodiscard]] bool busy() const noexcept { return step_ != Step::Idle; }

    [[nodiscard]] bool committed() const noexcept { return step_ != Step::Scale && whole_; }

    [[nodiscard]] FlipBuffer target() const noexcept { return target_; }

    BackdropSampler::Image read(
        BackdropSampler& sampler,
        std::int64_t now,
        const Canvas& c,
        bool reset
    ) noexcept;

    [[nodiscard]] HdRect scale_band() noexcept;

    void begin_copy(FlipBuffer buffer, HdRect box, const Canvas& c) noexcept;

    [[nodiscard]] HdRect copy_band() noexcept;

    void abort() noexcept;

    void reset_canvas() noexcept;

private:
    BackdropScaler& scaler_;
    Step step_ = Step::Idle;
    BackdropSampler::Image img_{};
    Canvas canvas_{};
    HdRect box_{};
    std::int32_t next_row_ = 0;
    FlipBuffer target_{};
    bool whole_ = true;
};

}
