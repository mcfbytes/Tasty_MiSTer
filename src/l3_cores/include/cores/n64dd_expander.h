// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "cores/core_window.h"
#include "cores/n64dd_disk_map.h"
#include "infra/error.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"

namespace mister::cores::n64dd {

class NddExpander {
    TASTY_SEAT_EXEMPT(component);

public:
    void start(const DiskMap& map) noexcept;

    [[nodiscard]] Ex<void> feed(std::span<const std::uint8_t> piece, ICoreWindow& window);

    [[nodiscard]] bool complete() const noexcept;

private:
    [[nodiscard]] Ex<void> place_(const DiskMap::Step& s, ICoreWindow& window);

    infra::OptRef<const DiskMap> map_{};
    std::size_t step_ = 0;
    std::uint32_t have_ = 0;
    std::array<std::uint8_t, kZone0BlockBytes> block_{};
    std::array<std::uint8_t, kBlockStride> image_{};
};

}
