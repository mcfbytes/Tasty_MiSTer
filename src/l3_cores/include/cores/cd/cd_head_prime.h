// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "svc/disc/disc_ask.h"
#include "svc/disc/disc_geometry.h"
#include "svc/disc/disc_read_service.h"
#include "svc/disc/disc_reader.h"

namespace mister::cores::cd {

struct HeadPrime {

    svc::disc::DiscForm form;

    bool pair = false;

    bool subcode = false;
};

inline void prime_head(
    svc::disc::DiscReadService& disc,
    svc::disc::Lba at,
    HeadPrime what
) noexcept {
    disc.hint(svc::disc::DiscAsk{.form = what.form, .lba = at});
    if (what.pair) {
        disc.hint(
            svc::disc::DiscAsk{.form = what.form, .lba = at + svc::disc::Lba::affine_count{1}}
        );
    }
    if (what.subcode)
        disc.hint(svc::disc::DiscAsk{.form = svc::disc::DiscForm::Subcode, .lba = at});
}

}
