// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>

#include "app/hd_osd_status.h"
#include "cores/pause_on_osd.h"
#include "proto/item_probe.h"
#include "proto/status_word.h"
#include "proto/types.h"

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
            if (probe->width == 0
                || probe->width > proto::ItemProbe::kMaxWidth
                || probe->lo.v + probe->width > 256u)
                return HdOsdStatus::Badge::Unknown;
            unsigned value = 0;
            for (unsigned i = 0; i < probe->width; ++i) {
                if (status.get_bit(proto::StatusBit{static_cast<std::uint8_t>(probe->lo.v + i)}))
                    value |= 1u << i;
            }
            return value == probe->active_value ? HdOsdStatus::Badge::Paused
                                                : HdOsdStatus::Badge::Running;
        }
    }
    return HdOsdStatus::Badge::Unknown;
}

}
