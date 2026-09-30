// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "cores/core_window.h"
#include "infra/error.h"
#include "infra/seat.h"
#include "hal/fabric_region.h"
#include "hal/fpga_aperture.h"
#include "hal/fpga_memory.h"
#include "hal/phys_region.h"

namespace mister::app {

class CoreWindow final : public cores::ICoreWindow {
    TASTY_SEAT_RESIDENT(RT);

public:
    [[nodiscard]] static Ex<CoreWindow> open_row(const hal::FpgaAperture& aperture,
                                                 const hal::FabricRegion& row);

    [[nodiscard]] static Ex<hal::FpgaMemory> map_row(const hal::FpgaAperture& aperture,
                                                     const hal::FabricRegion& row);

    explicit CoreWindow(hal::FpgaMemory window) noexcept;

    CoreWindow(CoreWindow&&) noexcept = default;
    CoreWindow& operator=(CoreWindow&&) noexcept = delete;
    CoreWindow(const CoreWindow&) = delete;
    CoreWindow& operator=(const CoreWindow&) = delete;

    [[nodiscard]] Ex<std::size_t> write(std::size_t off, std::span<const std::byte> src) override;
    [[nodiscard]] Ex<std::size_t> read(std::size_t off, std::span<std::byte> dst) const override;
    void publish() noexcept override;
    [[nodiscard]] std::size_t size() const noexcept override;

    [[nodiscard]] const hal::PhysRegion& region() const noexcept;

private:
    hal::FpgaMemory window_;
};

}  // namespace mister::app
