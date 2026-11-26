// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "hal/pin_sample.h"
#include "hal/types.h"

namespace mister::hal {

struct OcramPresence {
    [[nodiscard]] static constexpr OcramPresence of(GpiWord gpi) noexcept {
        return OcramPresence{gpi.test(kGpiOcramPresentField)};
    }

    [[nodiscard]] constexpr bool present() const noexcept { return present_; }

    friend constexpr bool operator==(OcramPresence, OcramPresence) = default;

    OcramPresence() = default;

private:
    explicit constexpr OcramPresence(bool present) noexcept : present_(present) {}

    bool present_ = false;
};

}
