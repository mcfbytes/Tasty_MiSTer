// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

#include "app/load_window_map.h"
#include "cores/core_window.h"
#include "infra/error.h"
#include "infra/seat.h"
#include "infra/seat_owner_io.h"
#include "infra/seat_owner_main.h"
#include "hal/fpga_memory.h"
#include "hal/phys_region.h"
#include "os/types.h"

namespace mister::app {

template <class Owner>
class LoadWindow final : public cores::ICoreWindow {
    TASTY_SEAT_FROM(Owner);

public:
    [[nodiscard]] static Ex<LoadWindow> open(
        ILoadWindowMap<Owner>& map,
        const hal::PhysRegion& aperture,
        os::PhysAddr addr,
        std::uint64_t len
    );

    LoadWindow(LoadWindow&&) noexcept = default;
    LoadWindow& operator=(LoadWindow&&) noexcept = delete;
    LoadWindow(const LoadWindow&) = delete;
    LoadWindow& operator=(const LoadWindow&) = delete;

    [[nodiscard]] Ex<std::size_t> write(std::size_t off, std::span<const std::byte> src) override;
    [[nodiscard]] Ex<std::size_t> read(std::size_t off, std::span<std::byte> dst) const override;

    [[nodiscard]] Ex<std::size_t> write_lanes(
        std::size_t off,
        std::span<const std::byte> src,
        std::size_t stride
    )
        requires std::same_as<Owner, seat_owner::Main>;
    void publish() noexcept override;
    [[nodiscard]] std::size_t size() const noexcept override;

    void handoff() noexcept
        requires std::same_as<Owner, seat_owner::Main>;

private:
    explicit LoadWindow(hal::FpgaMemory mem) noexcept : mem_(std::move(mem)) {}

    hal::FpgaMemory mem_;
};

extern template class LoadWindow<seat_owner::Main>;
extern template class LoadWindow<seat_owner::Io>;

}
