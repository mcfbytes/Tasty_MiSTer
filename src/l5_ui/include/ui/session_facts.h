// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/info_origin.h"
#include "app/session_identity.h"
#include "infra/fixed_str.h"
#include "proto/types.h"

namespace mister::ui {

struct SessionFacts {

    bool core_loaded = false;
    bool front_end = false;

    FixedStr<app::SessionIdentity::kMax, StrFit::Clip> core_dir{};

    bool hd_switchable = false;

    bool in_hd_tree = false;

    proto::OsdRow info_origin{app::kInfoOriginY};

    [[nodiscard]] bool shows_hd_switch() const noexcept { return hd_switchable && !in_hd_tree; }
};

}
