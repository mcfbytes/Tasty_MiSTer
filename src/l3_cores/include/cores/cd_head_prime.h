// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "svc/disc_geometry.h"
#include "svc/disc_read_service.h"
#include "svc/disc_reader.h"

namespace mister::cores {

struct HeadPrime {

    svc::DiscForm form;

    bool pair = false;

    bool subcode = false;
};

inline void prime_head(svc::DiscReadService& disc, std::int32_t lba, HeadPrime what) noexcept {
    if (lba < 0)
        return;
    const svc::Lba at{static_cast<std::uint32_t>(lba)};
    disc.hint(what.form, at);
    if (what.pair)
        disc.hint(what.form, svc::Lba{at.v + 1});
    if (what.subcode)
        disc.hint(svc::DiscForm::Subcode, at);
}

}
