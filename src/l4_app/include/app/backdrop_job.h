// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>

#include "app/backdrop_sampler.h"
#include "app/backdrop_scaler.h"
#include "app/hd_rect.h"
#include "infra/seat.h"

namespace mister::app {

class BackdropJob {
    TASTY_SEAT_RESIDENT(HdOsd);

public:
    static constexpr std::int32_t kBandRows = 64;
    enum class Step : std::uint8_t { Idle, Scale, Copy };

    struct Canvas {
        std::span<std::uint16_t> frame{};
        std::uint32_t stride_px = 0;
        HdRect game{};
        HdRect cover{};
    };

    explicit BackdropJob(BackdropScaler& scaler) noexcept : scaler_(scaler) {}

    BackdropJob(const BackdropJob&) = delete;
    BackdropJob& operator=(const BackdropJob&) = delete;

    [[nodiscard]] Step step() const noexcept { return step_; }

    [[nodiscard]] bool busy() const noexcept { return step_ != Step::Idle; }

    [[nodiscard]] bool committed() const noexcept { return step_ != Step::Scale && whole_; }

    [[nodiscard]] std::uint8_t target() const noexcept { return slot_; }

    BackdropSampler::Image read(
        BackdropSampler& sampler,
        std::int64_t now,
        const Canvas& c,
        bool reset
    ) noexcept;

    [[nodiscard]] HdRect scale_band() noexcept;

    void begin_copy(std::uint8_t slot, HdRect box, const Canvas& c) noexcept;

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
    std::uint8_t slot_ = 0;
    bool whole_ = true;
};

}
