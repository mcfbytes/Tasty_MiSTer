// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "hal/types.h"
#include "infra/error.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "proto/link/link.h"
#include "svc/video/geometry_gate.h"
#include "svc/video/shadow_mask_word.h"
#include "svc/video/video_sample.h"

namespace mister::svc::video {

class IFilterResend;
struct ScalerSlotMode;

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
    explicit ProbeOps(proto::link::Link& link) noexcept : link_(link) {}

    ProbeOps(const ProbeOps&) = delete;
    ProbeOps& operator=(const ProbeOps&) = delete;

    [[nodiscard]] Ex<hal::SpiWord> emit_filter_mode(ScalerSlotMode mode);

    [[nodiscard]] Ex<hal::SpiWord> emit_shadow_mask(ShadowMaskWord flag);

    [[nodiscard]] Ex<hal::SpiWord> emit_hdmi_int_probe();

    [[nodiscard]] Ex<hal::SpiWord> emit_gamma_probe();

    [[nodiscard]] Ex<hal::SpiWord> emit_afilter_probe();

    [[nodiscard]] Ex<GeometryPass> sample_video_geometry(
        bool force,
        GeometryGate& gate,
        IFilterResend& filters
    );

private:
    struct VresStep {
        ResWord res{};
        bool res_changed = false;
        bool vi_seen = false;
    };

    struct FbParStep {
        FbParCrc crc{};
        bool fb_changed = false;
    };

    struct VfilterStep {
        FltFlags flags{};
        bool tail_deferred = false;
    };

    [[nodiscard]] Ex<VresStep> read_vres_(const GeometryGate& gate, bool force, GeometryPass& pass);
    [[nodiscard]] Ex<FbParStep> read_fb_par_(
        const GeometryGate& gate,
        bool force,
        bool res_changed,
        GeometryPass& pass
    );
    [[nodiscard]] Ex<void> send_yc_(bool force, GeometryPass& pass);
    [[nodiscard]] Ex<VfilterStep> send_vfilter_(
        const GeometryGate& gate,
        bool resend,
        IFilterResend& filters,
        GeometryPass& pass
    );
    [[nodiscard]] Ex<void> send_scaling_(const ScalingPolicy& policy, const VideoSample& sample);

    proto::link::Link& link_;
};

}
