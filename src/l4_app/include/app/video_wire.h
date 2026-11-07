// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

#include "app/link_tx_channel.h"
#include "proto/conf_switches.h"
#include "hal/types.h"
#include "infra/opt_ref.h"
#include "proto/reset_fence.h"
#include "infra/counter.h"
#include "infra/error.h"
#include "infra/telemetry.h"
#include "infra/wake_flag.h"
#include "hal/boards_table.h"
#include "hal/link_timing.h"
#include "proto/link.h"
#include "reactor/tick.h"
#include "svc/filter_bank.h"
#include "svc/filter_resend.h"
#include "svc/filter_send_plan.h"
#include "svc/filter_set.h"
#include "svc/filter_store.h"
#include "svc/geometry_gate.h"
#include "svc/scaling_policy.h"
#include "svc/scaling_words.h"
#include "svc/audio_service.h"
#include "svc/video_service.h"
#include "infra/seat.h"
#include "infra/seq_cell.h"

namespace mister::app {

struct VideoGeoLatch {
    svc::VideoSample sample{};
    std::uint32_t core_seq = 0;
};

struct BlockPayload {
    std::uint16_t words[svc::kVideoWireWords]{};
    proto::UioOp opcode{};
    std::uint8_t count = 0;
};

static_assert(std::is_trivially_copyable_v<BlockPayload>);

struct ShadowMaskWord {
    std::uint32_t v = 0;
    friend constexpr bool operator==(ShadowMaskWord, ShadowMaskWord) = default;
};

static_assert(std::is_trivially_copyable_v<ShadowMaskWord>);

inline constexpr std::size_t kArCustomWords = 4;
inline constexpr unsigned kArCustomWordBits = 16;

using ArCustomWords = std::array<std::uint16_t, kArCustomWords>;

[[nodiscard]] constexpr std::uint64_t pack_ar_custom(const ArCustomWords& w) noexcept {
    std::uint64_t bits = 0;
    for (std::size_t i = 0; i < kArCustomWords; ++i)
        bits |= std::uint64_t{w[i]} << (i * kArCustomWordBits);
    return bits;
}

[[nodiscard]] constexpr ArCustomWords unpack_ar_custom(std::uint64_t bits) noexcept {
    ArCustomWords w{};
    for (std::size_t i = 0; i < kArCustomWords; ++i)
        w[i] = static_cast<std::uint16_t>(bits >> (i * kArCustomWordBits));
    return w;
}

inline constexpr std::uint16_t kFilterVerMask = 0x000F;

struct PreludeStats {
    std::uint32_t arms = 0;
    std::uint32_t completions = 0;
    std::uint32_t steps = 0;
    std::uint32_t chunks = 0;
    std::uint32_t coeff_words = 0;
    std::uint32_t parked = 0;
    std::uint32_t barrier_waits = 0;
    std::uint32_t barrier_timeouts = 0;
    std::uint32_t errors = 0;
    std::uint32_t coeff_skips = 0;
    std::uint32_t afilter_skips = 0;
    std::uint16_t flt_flags = 0;
    std::uint16_t fb_ack = 0;
    std::uint16_t hdmi_int = 0;
    std::uint16_t gamma_cap = 0;
    std::uint16_t af_flags = 0;
    std::uint16_t sm_cap = 0;

    std::uint16_t tail_fail = 0;
    std::uint8_t step = 0;
};

static_assert(std::is_trivially_copyable_v<PreludeStats>);

struct FilterStream {
    TASTY_SEAT_RESIDENT(RT);

    struct Ports {
        proto::Link* link;
        const svc::FilterSet& set;
        svc::FilterBank& sent_horiz;
        svc::FilterBank& sent_vert;
        PreludeStats& pre;
        xthread::Counter& streams;
        xthread::Counter& words;
        xthread::Counter& errors;
    };

    bool armed = false;
    bool generated = false;
    bool prelude = false;
    bool tail = false;
    std::uint16_t ver = 0;
    svc::FilterSendPlan plan{};
    std::uint8_t seg_i = 0;
    svc::VFilter vert_slot = svc::VFilter::Horz;
    std::size_t cursor = 0;
    svc::ScalingWords tail_words{};

    bool arm(
        const Ports& p,
        std::uint16_t flt_flags,
        svc::VFilter slot,
        bool in_prelude,
        bool with_tail,
        const svc::ScalingWords& tw
    ) noexcept;

    void arm_generated(std::uint16_t flt_flags, bool in_prelude) noexcept;

    void step(const Ports& p) noexcept;

    [[nodiscard]] bool active() const noexcept { return armed; }
};

class VideoWire : public svc::VideoService::IVideoWireSink,
                  public svc::VideoService::IResolutionSampler,
                  public svc::IFilterResend {
    TASTY_SEAT_MEDIATOR(Ui, RT);

public:
    static constexpr std::size_t kSlots = proto::kLinkTxCapacity + 1;
    static constexpr std::size_t kWords = svc::kVideoWireWords;

    static_assert(
        kSlots == proto::kLinkTxCapacity + 1,
        "item / video-rule: the block table is sized ONE LARGER than "
        "the ring the index rides, because xthread::SpscRing::pop frees "
        "the seat BEFORE emit() reads the block — the block outlives the "
        "op. Equal-sized re-opens the tear; smaller re-opens "
        "the aliasing gap. It is anchored to the ring the index actually "
        "rides, not to any other ring that happens to be the same size."
    );
    static_assert(kSlots <= 256, "LinkOp::slot is a uint8_t");

    using Block = xthread::SeqCell<BlockPayload>;

    struct Wiring {
        LinkTxChannel<SeatTag::Ui>& link_tx;
    };

    explicit VideoWire(Wiring w) noexcept : link_tx_(w.link_tx) {}

    VideoWire(const VideoWire&) = delete;
    VideoWire& operator=(const VideoWire&) = delete;

    [[nodiscard]] Ex<void> publish(
        proto::UioOp opcode,
        std::span<const std::uint16_t> words
    ) override;

    [[nodiscard]] Ex<void> stage(proto::UioOp opcode, std::span<const std::uint16_t> words);

    std::uint32_t staged_generation() const noexcept { return gen_; }

    [[nodiscard]] Ex<void> emit(
        proto::Link& link,
        std::uint8_t slot,
        std::uint32_t gen,
        infra::OptRef<proto::IResetFence> fence = {}
    );

    static constexpr std::int64_t kGeoFirstPeriodMs = 1000;
    static constexpr std::int64_t kGeoPeriodMs = 500;

    void on_rt_round(
        proto::Link& link,
        std::int64_t now_ns,
        infra::OptRef<proto::IResetFence> fence = {}
    );

    [[nodiscard]] Ex<svc::VideoSample> sample(bool force) override;

    [[nodiscard]] std::uint32_t sampled_core_seq() const noexcept { return sampled_seq_; }

    void publish_policy(const svc::ScalingPolicy& p) noexcept { policy_.publish(p.to_bits()); }

    svc::ScalingPolicy policy() const noexcept {
        return svc::ScalingPolicy::from_bits(policy_bits_);
    }

    void request_force() noexcept { force_.request(); }

    void reset_geometry(std::uint32_t core_seq) noexcept;

    static constexpr std::size_t kCoeffChunkWords = 64;
    static constexpr std::int64_t kRoundBudgetNs = reactor::kTickNs / 2;

    static constexpr std::uint32_t kBarrierRounds = 1000;

    enum class PreludeStep : std::uint8_t {
        Idle = 0,
        Fbuf,
        ArCust,
        FltNum,
        FltCoef,
        Burst,
        ShadowMask,
        Tail,
        Done
    };

    void arm_prelude() noexcept;

    [[nodiscard]] svc::AudioService& audio() noexcept { return audio_; }

    void set_key_map(proto::ConfSwitches conf) noexcept {
        tail_but_sw_ = static_cast<std::uint16_t>(
            (tail_but_sw_ & (kButton1 | kButton2)) | conf.word()
        );
    }

    std::uint16_t key_map() const noexcept { return tail_but_sw_; }

    static constexpr std::uint16_t kButton1 = 0x0001;
    static constexpr std::uint16_t kButton2 = 0x0002;
    bool update_but_sw(
        proto::Link& link,
        hal::ButtonMask buttons,
        bool osd_visible,
        infra::OptRef<proto::IResetFence> fence = {}
    ) noexcept;

    std::uint32_t but_sw_writes() const noexcept { return but_sw_writes_; }

    std::uint32_t but_sw_errors() const noexcept { return but_sw_errors_; }

    bool prelude_running() const noexcept {
        return pre_step_ != PreludeStep::Idle && pre_step_ != PreludeStep::Done;
    }

    void publish_filters(const svc::FilterSet& s) noexcept;

    bool plan_filter_send(const svc::VideoSample& s, bool resend) override;

    struct FilterStats {
        std::uint32_t published = 0;
        std::uint32_t consumed = 0;
        std::uint32_t waits = 0;
        std::uint32_t wait_timeouts = 0;
        std::uint32_t streams = 0;
        std::uint32_t stream_words = 0;
        std::uint32_t stream_errors = 0;
    };

    FilterStats filter_stats() const noexcept;

    void publish_ar_custom(const ArCustomWords& words) noexcept {
        ar_cust_.publish(pack_ar_custom(words));
    }

    void publish_shadow_mask(std::uint16_t flag_word) noexcept {
        shadow_mask_.publish(ShadowMaskWord{flag_word});
    }

    static constexpr std::uint16_t kTailFailProbe = 1u << 0;
    static constexpr std::uint16_t kTailFailFlush = 1u << 1;
    static constexpr std::uint16_t kTailFailPush = 1u << 2;
    static constexpr std::uint16_t kTailFailVol = 1u << 3;
    static constexpr std::uint16_t kTailFailButSw = 1u << 4;

    PreludeStats prelude_stats() const noexcept;

    struct GeoStats {
        std::uint32_t rounds = 0;
        std::uint32_t samples = 0;
        std::uint32_t changes = 0;
        std::uint32_t resends = 0;
        std::uint32_t errors = 0;
        std::uint32_t late = 0;
        std::uint32_t generation = 0;
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
    };

    Stats stats() const noexcept;

private:
    bool step_prelude(proto::Link& link, infra::OptRef<proto::IResetFence> fence);

    void publish_prelude() noexcept;

    bool consume_filters();
    bool service_filter_publish(proto::Link& link);

    [[nodiscard]] FilterStream::Ports stream_ports_(proto::Link* link) noexcept {
        return {
            .link = link,
            .set = flt_local_,
            .sent_horiz = sent_horiz_,
            .sent_vert = sent_vert_,
            .pre = pre_,
            .streams = flt_streams_,
            .words = flt_stream_words_,
            .errors = flt_stream_errors_
        };
    }

    svc::VFilter select_slot(const svc::VideoSample& s) const;

    void replay_parked_(
        proto::Link& link,
        infra::OptRef<proto::IResetFence> fence,
        bool with_but_sw
    ) noexcept;

    LinkTxChannel<SeatTag::Ui>& link_tx_;
    std::uint32_t gen_ = 0;
    Block table_[kSlots]{};

    xthread::Telemetry<VideoGeoLatch, SeatTag::RT> geo_cell_{};
    std::uint32_t geo_core_seq_ = 0;
    std::uint32_t sampled_seq_ = 0;

    xthread::Telemetry<std::uint64_t, SeatTag::Ui> policy_{};
    xthread::Telemetry<std::uint64_t, SeatTag::Ui> ar_cust_{};
    xthread::Telemetry<ShadowMaskWord, SeatTag::Ui> shadow_mask_{};
    xthread::Telemetry<std::uint64_t, SeatTag::Ui>::Reader policy_seen_{};
    xthread::Telemetry<std::uint64_t, SeatTag::Ui>::Reader ar_cust_seen_{};
    xthread::Telemetry<ShadowMaskWord, SeatTag::Ui>::Reader shadow_seen_{};
    std::uint64_t policy_bits_ = 0;
    std::uint64_t ar_cust_bits_ = 0;
    ShadowMaskWord shadow_mask_word_{};
    xthread::WakeFlag force_{};
    xthread::WakeFlag::Cursor force_seen_{};

    svc::GeometryGate geo_gate_{};

    xthread::Telemetry<svc::FilterSet, SeatTag::Ui> flt_cell_{};

    svc::FilterSet flt_local_{};
    bool flt_have_ = false;
    std::uint32_t flt_seen_gen_ = 0;
    svc::FilterBank sent_horiz_{};
    svc::FilterBank sent_vert_{};
    FilterStream fs_{};
    xthread::Counter flt_published_{};
    xthread::Counter flt_consumed_{};
    xthread::Counter flt_waits_{};
    xthread::Counter flt_wait_timeouts_{};
    xthread::Counter flt_streams_{};
    xthread::Counter flt_stream_words_{};
    xthread::Counter flt_stream_errors_{};

    PreludeStep pre_step_ = PreludeStep::Idle;
    std::uint32_t pre_barrier_ = 0;
    std::uint32_t pre_flt_barrier_ = 0;
    std::uint8_t pre_parked_slot_ = 0;
    std::uint32_t pre_parked_gen_ = 0;
    bool pre_parked_ = false;

    bool pre_replaying_ = false;

    bool pre_in_block_ = false;
    svc::AudioService audio_{svc::AudioService::create()};
    std::uint16_t tail_but_sw_ = 0;
    std::uint16_t but_sw_sent_ = 0;

    [[nodiscard]] Ex<void> emit_but_sw_(
        proto::Link& link,
        std::uint16_t word,
        infra::OptRef<proto::IResetFence> fence
    ) noexcept;

    std::uint16_t but_sw_shadow_ = 0;
    bool but_sw_seen_ = false;
    std::uint32_t but_sw_writes_ = 0;
    std::uint32_t but_sw_errors_ = 0;

    PreludeStats pre_{};
    xthread::Telemetry<PreludeStats, SeatTag::RT> prelude_cell_{};
    std::int64_t geo_next_ns_ = 0;

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
};

consteval bool coeff_chunk_fits_every_board() {
    return hal::every_measured(&hal::BoardProfile::timing, [](const hal::LinkTiming& g) consteval {
        const std::int64_t beat_ns = hal::LinkTimingValues::measured(g).beat_ns;
        return std::int64_t{VideoWire::kCoeffChunkWords + 1} * beat_ns <= VideoWire::kRoundBudgetNs;
    });
}

static_assert(
    coeff_chunk_fits_every_board(),
    "S3 chunk must leave half the 1 ms executive round free"
);

}
