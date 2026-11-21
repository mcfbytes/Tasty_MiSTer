// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>

#include "hal/spi_transport.h"
#include "hal/uio_frame_lane.h"
#include "proto/link_sel.h"

namespace mister::proto::link {

[[nodiscard]] constexpr hal::ChipSelect chip_select(LinkSel sel) noexcept {
    switch (sel) {
        case LinkSel::Io:
            return hal::ChipSelect::Io;
        case LinkSel::Fpga:
            return hal::ChipSelect::Fpga;
        case LinkSel::Osd:
            return hal::ChipSelect::Osd;
        case LinkSel::OsdHdmi:
            return hal::ChipSelect::OsdHdmi;
        case LinkSel::OsdVga:
            return hal::ChipSelect::OsdVga;
    }
    return hal::ChipSelect::None;
}

[[nodiscard]] constexpr std::optional<hal::UioFrameLane::Sel> lane_sel(LinkSel sel) noexcept {
    switch (sel) {
        case LinkSel::Io:
            return hal::UioFrameLane::Sel::Uio;
        case LinkSel::Fpga:
            return hal::UioFrameLane::Sel::Fpga;
        case LinkSel::OsdHdmi:
            return hal::UioFrameLane::Sel::OsdHdmi;
        case LinkSel::OsdVga:
            return hal::UioFrameLane::Sel::OsdVga;
        case LinkSel::Osd:
            break;
    }
    return std::nullopt;
}

static_assert(
    lane_sel(LinkSel::Io) == hal::UioFrameLane::Sel::Uio
        && lane_sel(LinkSel::Fpga) == hal::UioFrameLane::Sel::Fpga
        && lane_sel(LinkSel::OsdHdmi) == hal::UioFrameLane::Sel::OsdHdmi
        && lane_sel(LinkSel::OsdVga) == hal::UioFrameLane::Sel::OsdVga,
    "LinkSel and UioFrameLane::Sel: the same four selects"
);
static_assert(
    !lane_sel(LinkSel::Osd).has_value(),
    "OSD_EN alone is the select the lane's encoding cannot spell"
);

}
