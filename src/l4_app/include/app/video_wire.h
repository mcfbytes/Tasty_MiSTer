// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

#include "app/link_tx_channel.h"
#include "proto/conf_switches.h"
#include "proto/reset_fence.h"
#include "infra/error.h"
#include "infra/telemetry.h"
#include "hal/boards_table.h"
#include "hal/link_timing.h"
#include "hal/spi_transport.h"
#include "reactor/tick.h"
#include "svc/filter_bank.h"
#include "svc/filter_resend.h"
#include "svc/filter_send_plan.h"
#include "svc/filter_set.h"
#include "svc/filter_store.h"
#include "svc/geometry_gate.h"
#include "svc/scaling_policy.h"
#include "svc/scaling_words.h"
#include "svc/video_service.h"
#include "infra/seat.h"

namespace mister::svc {
class AudioService;
}

namespace mister::app {

struct VideoGeoLatch {
    svc::VideoSample sample{};
    std::uint32_t core_seq = 0;
};

class VideoWire : public svc::VideoService::IVideoWireSink,
                  public svc::VideoService::IResolutionSampler,
                  public svc::IFilterResend {
    TASTY_SEAT_MEDIATOR(Ui, RT);

public:
    static constexpr std::size_t kSlots = proto::kLinkTxCapacity + 1;
    static constexpr std::size_t kWords = svc::kVideoWireWords;

    static_assert(kSlots == proto::kLinkTxCapacity + 1,
                  "item / video-rule: the block table is sized ONE LARGER than "
                  "the ring the index rides, because xthread::SpscRing::pop frees "
                  "the seat BEFORE emit() reads the block — the block outlives the "
                  "op. Equal-sized re-opens the tear; smaller re-opens "
                  "the aliasing gap. It is anchored to the ring the index actually "
                  "rides, not to any other ring that happens to be the same size.");
    static_assert(kSlots <= 256, "LinkOp::slot is a uint8_t");

    struct Block {

        struct Payload {
            std::uint16_t words[kWords]{};
            std::uint8_t opcode = 0;
            std::uint8_t count = 0;
        };
        static_assert(std::is_trivially_copyable_v<Payload>);
        static constexpr std::size_t kBodyWord = sizeof(std::uint32_t);
        static constexpr std::size_t kBodyFull = sizeof(Payload) / kBodyWord;
        static constexpr std::size_t kBodyTail = sizeof(Payload) % kBodyWord;

        std::atomic<std::uint32_t> gen{0};

        std::atomic<std::uint32_t> body[kBodyFull + (kBodyTail != 0 ? 1 : 0)]{};
    };

    VideoWire() = default;
    VideoWire(const VideoWire&) = delete;
    VideoWire& operator=(const VideoWire&) = delete;

    Ex<void> publish(std::uint8_t opcode, std::span<const std::uint16_t> words) override;

    void attach_link_tx(LinkTxChannel& tx) noexcept { link_tx_ = &tx; }

    Ex<void> stage(std::uint8_t opcode, std::span<const std::uint16_t> words);

    std::uint32_t staged_generation() const noexcept { return gen_; }

    Ex<void> emit(hal::ISpiTransport& link, std::uint8_t slot, std::uint32_t gen);

    static constexpr std::int64_t kGeoFirstPeriodMs = 1000;
    static constexpr std::int64_t kGeoPeriodMs = 500;

    void on_rt_round(hal::ISpiTransport& link, std::int64_t now_ns);

    Ex<svc::VideoSample> sample(bool force) override;

    [[nodiscard]] std::uint32_t sampled_core_seq() const noexcept { return sampled_seq_; }

    void publish_policy(const svc::ScalingPolicy& p) noexcept {
        policy_.store(p.to_bits(), std::memory_order_release);
    }
    svc::ScalingPolicy policy() const noexcept {
        return svc::ScalingPolicy::from_bits(policy_.load(std::memory_order_acquire));
    }

    void request_force() noexcept {
        ++force_pub_;
        force_seq_.store(force_pub_, std::memory_order_release);
    }

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

    void attach_audio(svc::AudioService& a) noexcept { audio_ = &a; }

    void attach_reset_fence(proto::IResetFence& f) noexcept { fence_ = &f; }

    void set_key_map(proto::ConfSwitches conf) noexcept {
        tail_but_sw_ =
            static_cast<std::uint16_t>((tail_but_sw_ & (kButton1 | kButton2)) | conf.word());
    }
    std::uint16_t key_map() const noexcept { return tail_but_sw_; }

    static constexpr std::uint16_t kButton1 = 0x0001;
    static constexpr std::uint16_t kButton2 = 0x0002;
    bool update_but_sw(hal::ISpiTransport& link, std::uint16_t buttons, bool osd_visible) noexcept;
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

    void publish_ar_custom(std::uint16_t a0, std::uint16_t a1, std::uint16_t a2,
                           std::uint16_t a3) noexcept {
        const std::uint64_t bits =
            static_cast<std::uint64_t>(a0) | (static_cast<std::uint64_t>(a1) << 16) |
            (static_cast<std::uint64_t>(a2) << 32) | (static_cast<std::uint64_t>(a3) << 48);
        ar_cust_.store(bits, std::memory_order_release);
    }
    void publish_shadow_mask(std::uint16_t flag_word) noexcept {
        shadow_mask_.store(flag_word, std::memory_order_release);
    }

    static constexpr std::uint16_t kTailFailProbe = 1u << 0;
    static constexpr std::uint16_t kTailFailFlush = 1u << 1;
    static constexpr std::uint16_t kTailFailPush = 1u << 2;
    static constexpr std::uint16_t kTailFailVol = 1u << 3;
    static constexpr std::uint16_t kTailFailButSw = 1u << 4;

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

    static void bump(std::atomic<std::uint32_t>& c) noexcept {
        c.store(c.load(std::memory_order_relaxed) + 1u, std::memory_order_relaxed);
    }

    struct Stats {
        std::uint32_t staged = 0;
        std::uint32_t emitted = 0;
        std::uint32_t stale = 0;
        std::uint32_t partial = 0;
        std::uint32_t rejected = 0;

        std::uint32_t arm_fail = 0;
    };
    Stats stats() const noexcept;

private:
    bool step_prelude(hal::ISpiTransport& link);

    bool consume_filters();
    bool service_filter_publish(hal::ISpiTransport& link);
    bool arm_filter_stream(std::uint16_t flt_flags, std::size_t vert_slot, bool prelude, bool tail,
                           const svc::ScalingWords& tail_words);
    void step_filter_stream(hal::ISpiTransport& link);
    std::size_t select_slot(const svc::VideoSample& s) const;

    LinkTxChannel* link_tx_ = nullptr;
    std::uint32_t gen_ = 0;
    Block table_[kSlots]{};

    xthread::Telemetry<VideoGeoLatch> geo_cell_{};
    std::uint32_t geo_core_seq_ = 0;
    std::uint32_t sampled_seq_ = 0;

    static_assert(std::atomic<std::uint64_t>::is_always_lock_free,
                  "item/item: policy_ is a naturally-aligned 64-bit atomic read on "
                  "T-UI and written on T-RT with no lock — PROVE it here rather than "
                  "assume it, exactly as input_wire.h does for its own 64-bit cells.");
    std::atomic<std::uint64_t> policy_{0};
    std::atomic<std::uint32_t> force_seq_{0};
    std::uint32_t force_seen_ = 0;
    std::uint32_t force_pub_ = 0;

    svc::GeometryGate geo_gate_{};

    xthread::Telemetry<svc::FilterSet> flt_cell_{};

    svc::FilterSet flt_local_{};
    bool flt_have_ = false;
    std::uint32_t flt_seen_gen_ = 0;
    svc::FilterBank sent_horiz_{};
    svc::FilterBank sent_vert_{};
    bool fs_active_ = false;
    bool fs_generated_ = false;
    bool fs_prelude_ = false;
    bool fs_tail_ = false;
    std::uint16_t fs_ver_ = 0;
    svc::FilterSendPlan fs_plan_{};
    std::uint8_t fs_seg_i_ = 0;
    std::uint8_t fs_vert_slot_ = 0;
    std::size_t fs_cursor_ = 0;
    svc::ScalingWords fs_tail_words_{};
    std::uint32_t pre_flt_barrier_ = 0;
    std::atomic<std::uint32_t> flt_published_{0};
    std::atomic<std::uint32_t> flt_consumed_{0};
    std::atomic<std::uint32_t> flt_waits_{0};
    std::atomic<std::uint32_t> flt_wait_timeouts_{0};
    std::atomic<std::uint32_t> flt_streams_{0};
    std::atomic<std::uint32_t> flt_stream_words_{0};
    std::atomic<std::uint32_t> flt_stream_errors_{0};

    PreludeStep pre_step_ = PreludeStep::Idle;
    std::uint32_t pre_barrier_ = 0;
    std::uint8_t pre_parked_slot_ = 0;
    std::uint32_t pre_parked_gen_ = 0;
    bool pre_parked_ = false;

    bool pre_replaying_ = false;

    bool pre_in_block_ = false;
    svc::AudioService* audio_ = nullptr;
    proto::IResetFence* fence_ = nullptr;
    std::uint16_t tail_but_sw_ = 0;
    std::uint16_t but_sw_sent_ = 0;

    [[nodiscard]] Ex<void> emit_but_sw_(hal::ISpiTransport& link, std::uint16_t word) noexcept;

    std::uint16_t but_sw_shadow_ = 0;
    bool but_sw_seen_ = false;
    std::uint32_t but_sw_writes_ = 0;
    std::uint32_t but_sw_errors_ = 0;

    static_assert(std::atomic<std::uint64_t>::is_always_lock_free,
                  "ar_cust_ (publish_ar_custom) must be lock-free too");
    std::atomic<std::uint64_t> ar_cust_{0};
    std::atomic<std::uint32_t> shadow_mask_{0};
    std::atomic<std::uint32_t> pre_arms_{0};
    std::atomic<std::uint32_t> pre_completions_{0};
    std::atomic<std::uint32_t> pre_steps_{0};
    std::atomic<std::uint32_t> pre_chunks_{0};
    std::atomic<std::uint32_t> pre_coeff_words_{0};
    std::atomic<std::uint32_t> pre_parked_n_{0};
    std::atomic<std::uint32_t> pre_barrier_waits_{0};
    std::atomic<std::uint32_t> pre_barrier_timeouts_{0};
    std::atomic<std::uint32_t> pre_errors_{0};
    std::atomic<std::uint32_t> pre_coeff_skips_{0};
    std::atomic<std::uint32_t> pre_flt_flags_{0};
    std::atomic<std::uint32_t> pre_fb_ack_{0};
    std::atomic<std::uint32_t> pre_sm_cap_{0};

    std::atomic<std::uint32_t> pre_hdmi_int_{0};
    std::atomic<std::uint32_t> pre_gamma_cap_{0};
    std::atomic<std::uint32_t> pre_af_flags_{0};
    std::atomic<std::uint32_t> pre_afilter_skips_{0};
    std::atomic<std::uint32_t> pre_tail_fail_{0};
    std::int64_t geo_next_ns_ = 0;

    bool geo_force_next_ = true;
    std::atomic<std::uint32_t> geo_rounds_{0};
    std::atomic<std::uint32_t> geo_samples_{0};
    std::atomic<std::uint32_t> geo_changes_{0};
    std::atomic<std::uint32_t> geo_resends_{0};
    std::atomic<std::uint32_t> geo_errors_{0};
    std::atomic<std::uint32_t> geo_late_{0};
    std::atomic<std::uint32_t> geo_reads_{0};

    std::atomic<std::uint32_t> staged_{0};
    std::atomic<std::uint32_t> emitted_{0};
    std::atomic<std::uint32_t> stale_{0};
    std::atomic<std::uint32_t> partial_{0};
    std::atomic<std::uint32_t> rejected_{0};
    std::atomic<std::uint32_t> arm_fail_{0};
};

consteval bool coeff_chunk_fits_every_board() {
    return hal::every_measured(&hal::BoardProfile::timing, [](const hal::LinkTiming& g) consteval {
        const std::int64_t beat_ns = hal::LinkTimingValues::measured(g).beat_ns;
        return static_cast<std::int64_t>(VideoWire::kCoeffChunkWords + 1) * beat_ns <=
               VideoWire::kRoundBudgetNs;
    });
}
static_assert(coeff_chunk_fits_every_board(),
              "S3 chunk must leave half the 1 ms executive round free");

}  // namespace mister::app
