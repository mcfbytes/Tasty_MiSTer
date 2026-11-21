// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <span>

#include "cores/core_window_decl.h"
#include "cores/types.h"
#include "hal/types.h"
#include "infra/error.h"
#include "infra/prelude.h"
#include "os/types.h"

namespace mister::cores {

struct WindowSpot {
    CoreWindowSlot slot{};
    WindowOffset offset{};

    [[nodiscard]] constexpr Ex<hal::FabricAddr> address_in(
        std::span<const CoreWindowDecl> windows,
        os::PhysAddr aperture
    ) const {
        const auto window = slot.in(windows);
        if (!window)
            return std::unexpected(offset.refusal(Errc::bad_format, ERR_SITE()));
        const auto fabric = hal::FabricAddr::of(
            offset.past(aperture.advanced(window->region.offset))
        );
        if (!fabric)
            return std::unexpected(offset.refusal(Errc::aperture_range, ERR_SITE()));
        return fabric.value_or(hal::FabricAddr{});
    }

    friend constexpr bool operator==(const WindowSpot&, const WindowSpot&) = default;
};

}
