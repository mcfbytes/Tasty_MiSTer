// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "hal/scaler_buffers.h"
#include "hal/scaler_probe.h"
#include "infra/seat.h"
#include "os/clock.h"

namespace mister::app {

class BackdropSampler {
    TASTY_SEAT_RESIDENT(HdOsd);

public:
    static constexpr std::int64_t kPollNs = 4'000'000;

    struct Image {
        std::span<const std::byte> rgb{};
        std::uint16_t width = 0;
        std::uint16_t height = 0;
        std::uint16_t line = 0;
        bool live = false;
        bool fresh = false;
        bool lowlat = false;
        bool stuck = false;
        std::uint8_t stride_mib = 0;
        std::uint32_t torn = 0;
    };

    BackdropSampler(hal::ScalerBuffers window, const os::IClock* clock) noexcept;

    [[nodiscard]] bool note_applies(std::uint32_t applies, std::int64_t now) noexcept;
    void restart(std::int64_t now) noexcept;
    [[nodiscard]] bool probing() const noexcept { return phase_ == Phase::Probe; }
    [[nodiscard]] bool poll_due(std::int64_t now) const noexcept;
    [[nodiscard]] int park_ms(std::int64_t now) const noexcept;
    Image poll(std::int64_t now, bool copy) noexcept;

private:
    enum class Phase : std::uint8_t { Probe, Run, Idle };

    Image image_(bool fresh) const noexcept;
    [[nodiscard]] bool copy_frame_() noexcept;

    hal::ScalerBuffers window_;
    const os::IClock* clock_ = nullptr;
    hal::ScalerProbe probe_{};
    Phase phase_ = Phase::Probe;
    bool have_applies_ = false;
    bool lowlat_ = false;
    bool stuck_ = false;
    bool live_ = false;
    std::uint32_t applies_ = 0;
    std::uint32_t torn_ = 0;
    std::size_t stride_ = 0;
    std::uint8_t stride_mib_ = 0;
    std::int64_t next_poll_ = 0;
    std::uint16_t width_ = 0;
    std::uint16_t height_ = 0;
    std::uint16_t line_ = 0;
    std::size_t view_n_ = 0;
    std::vector<std::byte> cache_{};
};

}  // namespace mister::app
