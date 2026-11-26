// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <type_traits>
#include <utility>

#include "app/delivered.h"
#include "app/link_op_ctx.h"
#include "app/link_tx_channel.h"
#include "app/prelude_stats.h"
#include "app/staged_frame.h"
#include "proto/conf_switches.h"
#include "hal/types.h"
#include "infra/opt_ref.h"
#include "proto/reset_fence.h"
#include "infra/xthread/counter.h"
#include "infra/error.h"
#include "infra/prelude.h"
#include "infra/xthread/telemetry.h"
#include "infra/xthread/wake_flag.h"
#include "hal/boards_table.h"
#include "hal/link_timing.h"
#include "proto/link/frame_route.h"
#include "proto/link/link.h"
#include "proto/link_event.h"
#include "proto/link_port.h"
#include "proto/link_router.h"
#include "os/raw_monotonic_clock.h"
#include "reactor/tick.h"
#include "svc/video/filter_resend.h"
#include "svc/video/geometry_gate.h"
#include "svc/video/scaling_policy.h"
#include "svc/video/video_service.h"
#include "infra/seat.h"
#include "infra/xthread/seq_cell.h"

namespace mister::app {

struct VideoGeoLatch {
    svc::video::VideoSample sample{};
    CoreEdgeSeq core_seq{};
    ResendSeq filter_seq{};
    ResendSeq scaling_seq{};
};

struct PreludeLevel {
    proto::BindGeneration core{};
    bool settled = false;
};

struct BoundCore {
    proto::BindGeneration gen{};
    CoreEdgeSeq core_seq{};
};

class VideoWire : public svc::video::VideoService::IResolutionSampler,
                  public svc::video::IFilterResend {
    TASTY_SEAT_MEDIATOR(Ui, RT);

public:
    static constexpr std::size_t kSlots = proto::kLinkTxCapacity + 1;
    static constexpr std::size_t kWords = svc::video::kVideoWireWords;

    static_assert(
        kSlots == proto::kLinkTxCapacity + 1,
        "item / video-rule: the block table is sized ONE LARGER than "
        "the ring the index rides, because xthread::SpscRing::pop frees "
        "the seat BEFORE the frame reads the block — the block outlives "
        "the op. Equal-sized re-opens the tear; smaller re-opens "
        "the aliasing gap. It is anchored to the ring the index actually "
        "rides, not to any other ring that happens to be the same size."
    );
    static_assert(kSlots <= 256, "LinkOp::slot is a uint8_t");

    using Block = xthread::SeqCell<StagedFrame>;

    struct Wiring {
        LinkTxChannel<SeatTag::Ui>& link_tx;
        proto::ILinkRouter& replies;
    };

    static constexpr std::size_t kFramesInFlight = 16;

    static constexpr std::chrono::milliseconds kSettleBound{2000};

    using Port = proto::LinkPort<proto::LinkEvent::FrameReply>;

    explicit VideoWire(Wiring w) noexcept : link_tx_(w.link_tx), replies_(w.replies) {}

    VideoWire(const VideoWire&) = delete;
    VideoWire& operator=(const VideoWire&) = delete;

    [[nodiscard]] Ex<void> stage(const StagedFrame& f, proto::BindGeneration core);

    [[nodiscard]] bool may_stage() const noexcept {
        return link_tx_.ring().size() < kFramesInFlight;
    }

    [[nodiscard]] std::optional<BoundCore> bound_core() const noexcept;

    void publish_prelude_level(PreludeLevel l) noexcept { level_cell_.publish(l); }

    [[nodiscard]] proto::StageGen staged_generation() const noexcept { return gen_; }

    using Result = Delivered;

    [[nodiscard]] Delivered on(
        const proto::LinkOp::StagedFrame& op,
        const LinkOpCtx& ctx,
        proto::link::Link& link
    ) noexcept;
    [[nodiscard]] Delivered misrouted(
        const proto::LinkOp& op,
        const LinkOpCtx& ctx,
        proto::link::Link& link
    ) noexcept;

    void bind_core(
        CoreEdgeSeq seq,
        proto::BindGeneration gen,
        os::RawMonotonicClock::time_point now
    ) noexcept;

    void take_ui_cells() noexcept;

    [[nodiscard]] bool prelude_settled(os::RawMonotonicClock::time_point now) noexcept;

    static constexpr std::chrono::milliseconds kGeoFirstPeriod{1000};
    static constexpr std::chrono::milliseconds kGeoPeriod{500};

    void on_rt_round(
        proto::link::Link& link,
        std::int64_t now_ns,
        infra::OptRef<proto::IResetFence> fence = {}
    );

    [[nodiscard]] Ex<svc::video::VideoSample> sample() override;

    [[nodiscard]] CoreEdgeSeq sampled_core_seq() const noexcept { return sampled_seq_; }

    void publish_policy(const svc::video::ScalingPolicy& p) noexcept { policy_cell_.publish(p); }

    svc::video::ScalingPolicy policy() const noexcept { return policy_; }

    void request_force() noexcept override { force_.request(); }

    void reset_geometry(CoreEdgeSeq core_seq) noexcept;

    static constexpr std::chrono::nanoseconds kRoundBudgetNs{reactor::kTickNs / 2};

    void set_key_map(proto::ConfSwitches conf) noexcept {
        tail_but_sw_ = conf.with_buttons(tail_but_sw_.buttons());
    }

    [[nodiscard]] proto::ConfSwitches key_map() const noexcept { return tail_but_sw_; }

    bool update_but_sw(
        proto::link::Link& link,
        hal::ButtonMask buttons,
        bool osd_visible,
        infra::OptRef<proto::IResetFence> fence = {}
    ) noexcept;

    void bind_frames(infra::OptRef<proto::link::FrameRoute> frames) noexcept { frames_ = frames; }

    std::uint32_t but_sw_writes() const noexcept { return but_sw_writes_; }

    std::uint32_t but_sw_errors() const noexcept { return but_sw_errors_; }

    bool plan_filter_send(const svc::video::VideoSample& s, bool resend) override;

    [[nodiscard]] std::optional<VideoGeoLatch> geo_latch() const noexcept;

    [[nodiscard]] svc::video::ScalingPolicy published_policy() const noexcept;

    struct GeoStats {
        std::uint32_t rounds = 0;
        std::uint32_t samples = 0;
        std::uint32_t changes = 0;
        std::uint32_t resends = 0;
        std::uint32_t errors = 0;
        std::uint32_t late = 0;
        infra::CellGen generation{};
        std::uint32_t reads = 0;
        std::uint32_t torn = 0;
    };

    GeoStats geo_stats() const noexcept;

    struct Stats {
        std::uint32_t staged = 0;
        std::uint32_t emitted = 0;
        std::uint32_t stale = 0;
        std::uint32_t partial = 0;
        std::uint32_t rejected = 0;
        std::uint32_t staged_rejected = 0;
        std::uint32_t emit_rejected = 0;
        std::uint32_t arm_fail = 0;
        std::uint32_t stale_core = 0;
        std::uint32_t reply_drops = 0;
        std::uint32_t settle_bound_fires = 0;
    };

    Stats stats() const noexcept;

private:
    struct FrameArms {
        using Result = Delivered;
        proto::link::Link& link;
        infra::OptRef<proto::IResetFence> fence;
        Port& replies;
        VideoWire& self;
        const proto::LinkOp::StagedFrame& op;

        void answer_(
            proto::LinkEvent::ReplyWindow w,
            proto::WindowFails fails,
            proto::ReplyWord word
        ) noexcept;

        void answer_mode_(proto::link::Link& l) noexcept;

        [[nodiscard]] Delivered on(const StagedFrame::Mode& m) noexcept;
        [[nodiscard]] Delivered on(const StagedFrame::ModeInBlock& m) noexcept;
        [[nodiscard]] Delivered on(const StagedFrame::FbufProbes& f) noexcept;
        [[nodiscard]] Delivered on(const StagedFrame::FilterMode& m) noexcept;
        [[nodiscard]] Delivered on(const StagedFrame::CoeffChunk& c) noexcept;
        [[nodiscard]] Delivered on(const StagedFrame::ShadowMask& s) noexcept;
        [[nodiscard]] Delivered on(const StagedFrame::AudioTail& t) noexcept;
        [[nodiscard]] Delivered on(const StagedFrame::ScalingPair& p) noexcept;
        [[nodiscard]] Delivered on(const StagedFrame::AudVol& a) noexcept;
        [[nodiscard]] Delivered on(const StagedFrame::AfilterPush& a) noexcept;
        [[nodiscard]] Delivered misrouted(const StagedFrame& f) noexcept;
    };

    [[nodiscard]] Delivered run_frame_(
        const StagedFrame& f,
        const proto::LinkOp::StagedFrame& op,
        proto::link::Link& link,
        infra::OptRef<proto::IResetFence> fence
    ) noexcept;

    LinkTxChannel<SeatTag::Ui>& link_tx_;
    Port replies_;
    proto::StageGen gen_{};
    Block table_[kSlots]{};

    proto::BindGeneration bound_{};
    bool bound_once_ = false;
    os::RawMonotonicClock::time_point bound_at_{};
    bool settle_bound_fired_ = false;
    PreludeLevel level_{};
    xthread::Telemetry<BoundCore, SeatTag::RT> bound_cell_{};
    xthread::Telemetry<PreludeLevel, SeatTag::Ui> level_cell_{};
    xthread::Telemetry<PreludeLevel, SeatTag::Ui>::Reader level_seen_{};

    xthread::Telemetry<VideoGeoLatch, SeatTag::RT> geo_cell_{};
    CoreEdgeSeq geo_core_seq_{};
    CoreEdgeSeq sampled_seq_{};

    ResendSeq filter_seq_{};
    ResendSeq scaling_seq_{};

    xthread::Telemetry<svc::video::ScalingPolicy, SeatTag::Ui> policy_cell_{};
    xthread::Telemetry<svc::video::ScalingPolicy, SeatTag::Ui>::Reader policy_seen_{};
    svc::video::ScalingPolicy policy_{};
    xthread::WakeFlag force_{};
    xthread::WakeFlag::Cursor force_seen_{};

    svc::video::GeometryGate geo_gate_{};

    proto::ConfSwitches tail_but_sw_{};
    proto::ConfSwitches but_sw_sent_{};
    infra::OptRef<proto::link::FrameRoute> frames_{};
    [[nodiscard]] Ex<void> emit_but_sw_(
        proto::link::Link& link,
        proto::ConfSwitches word,
        infra::OptRef<proto::IResetFence> fence
    ) noexcept;
    proto::ConfSwitches but_sw_shadow_{};
    bool but_sw_seen_ = false;
    std::uint32_t but_sw_writes_ = 0;
    std::uint32_t but_sw_errors_ = 0;

    std::optional<os::RawMonotonicClock::time_point> geo_next_{};
    bool geo_force_next_ = true;
    xthread::Counter geo_rounds_{};
    xthread::Counter geo_samples_{};
    xthread::Counter geo_changes_{};
    xthread::Counter geo_resends_{};
    xthread::Counter geo_errors_{};
    xthread::Counter geo_late_{};
    xthread::Counter geo_reads_{};

    xthread::Counter staged_{};
    xthread::Counter emitted_{};
    xthread::Counter stale_{};
    xthread::Counter partial_{};
    xthread::Counter staged_rejected_{};
    xthread::Counter emit_rejected_{};
    xthread::Counter arm_fail_{};
    xthread::Counter stale_core_{};
    xthread::Counter reply_drops_{};
    xthread::Counter settle_bound_fires_{};
};

consteval bool coeff_chunk_fits_every_board() {
    return hal::every_measured(&hal::BoardProfile::timing, [](const hal::LinkTiming& g) consteval {
        const std::chrono::nanoseconds beat = hal::LinkTimingValues::measured(g).beat_ns;
        const std::int64_t beats = std::int64_t{kCoeffChunkWords} + 1;
        return beat * beats <= VideoWire::kRoundBudgetNs;
    });
}

static_assert(
    coeff_chunk_fits_every_board(),
    "S3 chunk must leave half the 1 ms executive round free"
);

}
