// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/error.h"
#include "infra/seat.h"
#include "proto/link.h"
#include "svc/geometry_gate.h"
#include "svc/video_sample.h"

namespace mister::svc {

class IFilterResend;

struct GeometryPass {
    VideoSample sample{};
    bool res_changed = false;
    bool fb_changed = false;
    bool changed = false;
    bool resent = false;
    bool yc_sent = false;
};

class ProbeOps final {
    TASTY_SEAT_RESIDENT(RT);

public:
    explicit ProbeOps(proto::Link& link) noexcept : link_(link) {}

    ProbeOps(const ProbeOps&) = delete;
    ProbeOps& operator=(const ProbeOps&) = delete;

    [[nodiscard]] Ex<std::uint16_t> emit_filter_mode(std::uint8_t mode);

    [[nodiscard]] Ex<std::uint16_t> emit_shadow_mask(std::uint16_t flag_word);

    [[nodiscard]] Ex<std::uint16_t> emit_hdmi_int_probe();

    [[nodiscard]] Ex<std::uint16_t> emit_gamma_probe();

    [[nodiscard]] Ex<std::uint16_t> emit_afilter_probe();

    [[nodiscard]] Ex<GeometryPass> sample_video_geometry(
        bool force,
        GeometryGate& gate,
        IFilterResend& filters
    );

private:
    proto::Link& link_;
};

}
