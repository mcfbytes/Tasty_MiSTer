// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <span>

#include "hal/fpga_memory.h"
#include "hal/phys_region.h"
#include "hal/scaler_header.h"
#include "infra/error.h"
#include "infra/seat.h"

namespace mister::hal {

class ScalerBuffers {
    TASTY_SEAT_EXEMPT(component);

public:
    static constexpr std::size_t kBuffers = 3;

    static constexpr std::size_t kStrideLarge = 0x0080'0000u;
    static constexpr std::size_t kStrideSmall = 0x0020'0000u;
    static constexpr std::size_t kSpanBytes = kBuffers * kStrideLarge;

    [[nodiscard]] static constexpr PhysRegion span_of(const PhysRegion& scaler_out) noexcept {
        return PhysRegion{scaler_out.phys, scaler_out.len == 0 ? 0 : kSpanBytes, "scaler-triple"};
    }

    [[nodiscard]] static Ex<ScalerBuffers> map(const PhysRegion& scaler_out);

    [[nodiscard]] static ScalerBuffers borrow(std::span<std::byte> backing);

    ScalerBuffers(ScalerBuffers&&) noexcept = default;
    ScalerBuffers& operator=(ScalerBuffers&&) noexcept = default;
    ScalerBuffers(const ScalerBuffers&) = delete;
    ScalerBuffers& operator=(const ScalerBuffers&) = delete;
    ~ScalerBuffers() = default;

    [[nodiscard]] Ex<ScalerHeader> header(std::size_t off) const;

    [[nodiscard]] Ex<void> copy(std::size_t off, std::span<std::byte> dst);
    [[nodiscard]] std::size_t len() const noexcept { return mem_.region().len; }

private:
    explicit ScalerBuffers(FpgaMemory m) noexcept : mem_(std::move(m)) {}
    FpgaMemory mem_;
};

void copy_uncached(std::byte* dst, const std::byte* src, std::size_t n) noexcept;

}  // namespace mister::hal
