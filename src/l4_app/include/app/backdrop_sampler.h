// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "app/rgb_rows.h"
#include "hal/scaler_buffers.h"
#include "hal/scaler_header.h"
#include "hal/scaler_probe.h"
#include "infra/park_timeout.h"
#include "infra/seat.h"

namespace mister::app {

class BackdropSampler {
    TASTY_SEAT_RESIDENT(HdOsd);

public:
    static constexpr std::int64_t kPollNs = 4'000'000;

    struct Image {
        RgbRows rgb{};
        std::uint16_t width = 0;
        bool live = false;
        bool fresh = false;
        bool same = false;
        bool lowlat = false;
        bool stuck = false;
        std::uint16_t stride_mib = 0;
        std::uint32_t torn = 0;
    };

    explicit BackdropSampler(hal::ScalerBuffers window) noexcept;

    [[nodiscard]] bool note_applies(std::uint32_t applies, std::int64_t now) noexcept;
    void restart(std::int64_t now) noexcept;

    [[nodiscard]] bool probing() const noexcept { return phase_ == Phase::Probe; }

    [[nodiscard]] bool poll_due(std::int64_t now) const noexcept;
    [[nodiscard]] infra::ParkTimeout park_ms(std::int64_t now) const noexcept;
    Image poll(std::int64_t now, bool copy) noexcept;

private:
    enum class Phase : std::uint8_t { Probe, Run, Idle };

    using Marks = std::array<std::optional<hal::ScalerHeader>, hal::ScalerBuffers::kBuffers>;

    void run_(hal::ScalerProbe::Verdict verdict, bool still) noexcept;
    [[nodiscard]] Marks marks_() const noexcept;
    Image image_(bool fresh) const noexcept;
    [[nodiscard]] bool copy_frame_() noexcept;

    hal::ScalerBuffers window_;
    hal::ScalerProbe probe_{};
    Phase phase_ = Phase::Probe;
    bool have_applies_ = false;
    bool lowlat_ = false;
    bool stuck_ = false;
    bool still_ = false;
    bool live_ = false;
    bool have_prev_ = false;
    bool same_ = false;
    std::uint32_t applies_ = 0;
    std::uint32_t torn_ = 0;
    hal::ScalerStride stride_ = hal::ScalerStride::Large;
    std::optional<hal::ScalerStride> known_stride_{};
    std::int64_t next_poll_ = 0;
    std::uint16_t width_ = 0;
    std::uint16_t height_ = 0;
    std::uint16_t line_ = 0;
    std::size_t view_n_ = 0;
    Marks still_marks_{};
    std::vector<std::byte> cache_{};
    std::vector<std::byte> prev_{};
};

}
