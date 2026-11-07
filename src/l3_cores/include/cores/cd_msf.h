// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "cores/types.h"
#include "svc/msf.h"

namespace mister::cores {

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

}
