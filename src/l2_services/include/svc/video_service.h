// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>
#include <string_view>

#include "infra/error.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "proto/uio_opcodes.h"
#include "svc/adv7513_io.h"
#include "svc/filter_kernel.h"
#include "svc/geometry_gate.h"
#include "svc/hdmi_link.h"
#include "svc/modeline.h"
#include "svc/modeline_item.h"
#include "svc/parsed_mode.h"
#include "svc/pll_block.h"
#include "svc/types.h"
#include "svc/video_sample.h"
#include "svc/video_timing.h"
#include "svc/video_wire_emit.h"
#include "svc/yc_table.h"

namespace mister::svc {

class VideoService {
    TASTY_SEAT_RESIDENT(Ui);

public:
    struct Wiring;

    VideoService(const VideoService&) = delete;
    VideoService& operator=(const VideoService&) = delete;

    explicit VideoService(Wiring w, adv7513::Io io) noexcept;
    [[nodiscard]] static VideoService create(Wiring w, adv7513::Io io);

    [[nodiscard]] static VideoService create();

    [[nodiscard]] Ex<video_timing::ParsedMode> parse_mode(std::string_view spec) const;

    [[nodiscard]] Ex<void> apply(const Modeline& m, PllBlock pll, Modeline::WireOptions opt);

    [[nodiscard]] Ex<void> poll_resolution();

    HdmiLink& hdmi() noexcept { return hdmi_; }

    class IVideoWireSink {
    public:
        virtual ~IVideoWireSink() = default;
        [[nodiscard]] virtual Ex<void> publish(
            proto::UioOp opcode,
            std::span<const std::uint16_t> words
        ) = 0;

    protected:
        IVideoWireSink() = default;
        IVideoWireSink(const IVideoWireSink&) = default;
        IVideoWireSink& operator=(const IVideoWireSink&) = default;
    };

    class IResolutionSampler {
    public:
        virtual ~IResolutionSampler() = default;
        [[nodiscard]] virtual Ex<VideoSample> sample() = 0;
        virtual void request_force() noexcept = 0;

    protected:
        IResolutionSampler() = default;
        IResolutionSampler(const IResolutionSampler&) = default;
        IResolutionSampler& operator=(const IResolutionSampler&) = default;
    };

    struct Wiring {
        infra::OptRef<IVideoWireSink> sink{};
        infra::OptRef<IResolutionSampler> sampler{};
    };

    void set_supports_pr(bool v) noexcept { supports_pr_ = v; }

    bool supports_pr() const noexcept { return supports_pr_; }

    const VideoSample& info() const noexcept { return gate_.last; }

    Generation resolution_generation() const noexcept { return res_gen_; }

    void request_force_sample() noexcept {
        if (sampler_)
            sampler_->request_force();
    }

    void reset_resolution_gate() noexcept {
        gate_ = GeometryGate{};
        if (sampler_)
            sampler_->request_force();
    }

private:
    HdmiLink hdmi_;

    infra::OptRef<IVideoWireSink> sink_{};
    infra::OptRef<IResolutionSampler> sampler_{};

    GeometryGate gate_{};
    Generation res_gen_{};
    bool supports_pr_ = true;
};

}
