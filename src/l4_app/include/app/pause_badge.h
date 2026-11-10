// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>

#include "app/hd_osd_status.h"
#include "cores/pause_on_osd.h"
#include "proto/item_probe.h"
#include "proto/status_word.h"

namespace mister::app {

[[nodiscard]] constexpr HdOsdStatus::Badge pause_badge(
    std::optional<proto::ItemProbe> probe,
    cores::PauseOnOsd rule,
    const proto::StatusWord& status,
    bool core_sees_osd
) noexcept {
    if (!core_sees_osd)
        return HdOsdStatus::Badge::Unknown;
    switch (rule) {
        case cores::PauseOnOsd::Always:
            return HdOsdStatus::Badge::Paused;
        case cores::PauseOnOsd::Option: {
            if (!probe)
                return HdOsdStatus::Badge::Unknown;
            const bool on = status.field(probe->bits) == probe->active;
            return on ? HdOsdStatus::Badge::Paused : HdOsdStatus::Badge::Running;
        }
    }
    return HdOsdStatus::Badge::Unknown;
}

}
