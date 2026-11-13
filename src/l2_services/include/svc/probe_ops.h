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
struct ScalerSlotMode;
struct ShadowMaskWord;

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

    [[nodiscard]] Ex<std::uint16_t> emit_filter_mode(ScalerSlotMode mode);

    [[nodiscard]] Ex<std::uint16_t> emit_shadow_mask(ShadowMaskWord flag);

    [[nodiscard]] Ex<std::uint16_t> emit_hdmi_int_probe();

    [[nodiscard]] Ex<std::uint16_t> emit_gamma_probe();

    [[nodiscard]] Ex<std::uint16_t> emit_afilter_probe();

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

    proto::Link& link_;
};

}
