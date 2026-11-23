// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>

#include "app/info_origin.h"
#include "app/info_plane.h"
#include "app/core_name.h"
#include "infra/seat.h"
#include "proto/types.h"
#include "ui/osd_tree.h"

namespace mister::ui {

struct SessionFacts {
    TASTY_SEAT_RESIDENT(Ui);

    bool core_loaded = false;
    bool front_end = false;

    app::CoreName core_dir{};

    bool hd_switchable = false;

    bool console_view = false;

    OsdTree tree = OsdTree::Classic;

    proto::OsdRow info_origin{app::kInfoOriginY};

    std::optional<app::InfoPlane> info_plane{};

    bool autosave = false;

    [[nodiscard]] bool shows_hd_switch() const noexcept {
        return hd_switchable && tree == OsdTree::Classic;
    }
};

}
