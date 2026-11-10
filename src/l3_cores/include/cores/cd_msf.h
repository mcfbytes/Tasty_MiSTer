// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "cores/types.h"
#include "svc/msf.h"

namespace mister::cores {

constexpr HeadLba HeadLba::of(svc::Msf msf) noexcept {
    constexpr std::int32_t kFps = static_cast<std::int32_t>(svc::kFramesPerSecond);
    constexpr std::int32_t kPerMinute = static_cast<std::int32_t>(svc::kFramesPerMinute);
    return HeadLba{
        static_cast<std::int32_t>(msf.m) * kPerMinute
        + static_cast<std::int32_t>(msf.s) * kFps
        + static_cast<std::int32_t>(msf.f)
    };
}

constexpr svc::Msf SectorCount::stock_msf() const noexcept {
    return svc::Msf::of_lba_stock(v);
}

constexpr svc::Msf SectorCount::wrapped_msf() const noexcept {
    return svc::Msf::of_wrapped(v);
}

constexpr svc::Msf HeadLba::msf() const noexcept {
    return svc::Msf::of_lba(v);
}

constexpr svc::Msf HeadLba::stock_msf() const noexcept {
    return svc::Msf::of_lba_stock(v);
}

constexpr svc::Msf HeadLba::wrapped_msf() const noexcept {
    return svc::Msf::of_wrapped(v);
}

static_assert(
    HeadLba::of(svc::Msf::of_lba(150)) == HeadLba{150},
    "the two absolute MSF directions invert"
);

}
