// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "svc/disc_ask.h"
#include "svc/disc_geometry.h"
#include "svc/disc_read_service.h"
#include "svc/disc_reader.h"

namespace mister::cores {

struct HeadPrime {

    svc::DiscForm form;

    bool pair = false;

    bool subcode = false;
};

inline void prime_head(svc::DiscReadService& disc, svc::Lba at, HeadPrime what) noexcept {
    disc.hint(svc::DiscAsk{.form = what.form, .lba = at});
    if (what.pair) {
        disc.hint(svc::DiscAsk{.form = what.form, .lba = at + svc::Lba::affine_count{1}});
    }
    if (what.subcode)
        disc.hint(svc::DiscAsk{.form = svc::DiscForm::Subcode, .lba = at});
}

}
