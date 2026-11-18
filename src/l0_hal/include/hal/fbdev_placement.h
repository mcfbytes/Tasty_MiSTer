// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>

#include "hal/fbdev_probe.h"
#include "hal/memory_model.h"
#include "hal/phys_region.h"
#include "hal/types.h"
#include "os/framebuffer.h"
#include "os/mmio_region.h"

namespace mister::hal {

inline constexpr os::MapLength kFramebufferHeader{4096};

class FbdevPlacement {
public:
    [[nodiscard]] static constexpr std::optional<FbdevPlacement> of(
        const PhysRegion& r,
        const os::Framebuffer::Window& w
    ) noexcept {
        if (w.start.past(r.phys) != kFramebufferHeader)
            return std::nullopt;
        const std::optional<os::MapLength> len = os::MapLength{r.len}.less(kFramebufferHeader);
        if (!len || len->empty() || w.len < *len)
            return std::nullopt;
        return FbdevPlacement{r, *len};
    }

    [[nodiscard]] constexpr const PhysRegion& region() const noexcept { return region_; }

    [[nodiscard]] constexpr os::MapLength len() const noexcept { return len_; }

private:
    constexpr FbdevPlacement(const PhysRegion& r, os::MapLength len) noexcept
        : region_(r),
          len_(len) {}

    PhysRegion region_;
    os::MapLength len_;
};

[[nodiscard]] std::optional<FbdevPlacement> place_on_fbdev(
    const FbdevProbe& probe,
    WriteCombine wc,
    const PhysRegion& r
) noexcept;

}
