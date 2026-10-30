// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>

#include "app/cmd_fifo.h"
#include "reactor/frame_clock.h"
#include "reactor/round_timing.h"
#include "app/mgl_pump.h"
#include "app/osd_wire.h"
#include "app/doorbell_cell.h"
#include "app/event.h"
#include "app/diag_counters.h"
#include "app/fallback_counts.h"
#include "app/core_builder.h"
#include "app/sector_grant.h"
#include "app/sector_route.h"
#include "app/osd_route.h"
#include "app/sys_route.h"
#include "app/load_window_counts.h"
#include "app/video_pump.h"
#include "app/avi_write_status.h"
#include "app/encode_status.h"
#include "app/rec_write_status.h"
#include "app/recorder_status.h"
#include "app/replay_status.h"
#include "app/hd_osd_status.h"
#include "diag_records.h"
#include "rt_evidence.h"
#include "infra/counter.h"
#include "infra/diag_log.h"
#include "infra/log_lane.h"
#include "infra/opt_ref.h"
#include "infra/rt_stats.h"
#include "infra/wake_flag.h"
#include "hal/thread_map.h"
#include "infra/seat.h"
#include "infra/telemetry.h"

namespace mister::app {
class IFrameSource;
class InputBuild;
struct InputSample;
class ScreenshotQueue;
class VideoWire;
}

namespace mister::svc {
class ChdPrefetch;
}

namespace mister::fw {

inline constexpr std::uint64_t kRssSoftCeilingBytes = 96ull * 1024 * 1024;

[[nodiscard]] constexpr std::uint64_t rss_ceiling_bytes(
    std::uint32_t capture_arena_kib,
    std::uint32_t chunk_arena_kib
) noexcept {
    return kRssSoftCeilingBytes
        + (static_cast<std::uint64_t>(capture_arena_kib) + chunk_arena_kib) * 1024u;
}

struct DiagStats {
    xthread::Counter drains{};
    xthread::Telemetry<std::uint64_t, SeatTag::Diag> heartbeat_last{};
    xthread::Counter heartbeat_stalls{};
    xthread::Telemetry<std::uint64_t, SeatTag::Diag> rss_bytes{};
    xthread::Counter rss_over_ceiling{};
};

class DiagSampler {
    TASTY_SEAT_RESIDENT(Diag);

public:
    static constexpr SeatTag kSeat = SeatTag::Diag;

    template <class T>
    using Main = xthread::Telemetry<T, SeatTag::Main>;

    struct DeviceCells {
        const Main<std::uint32_t>& mgl_row0;
        const Main<app::LoadWindowCounts>& window_counts;
        const app::BayCountsCell& bay_counts;
        const app::DiagCountersCell& diag;
        const Main<std::uint32_t>& pause_expiries;
        const Main<std::uint32_t>& recover_polls;
        const Main<std::uint32_t>& save_write_failures;
        const Main<app::FallbackCounts>& fallbacks;
        const Main<std::uint32_t>& ini_write_failures;
        const app::DoorbellStatsCell& doorbell;
        const reactor::RoundTimingCell& round_timing;
        infra::OptRef<const reactor::FrameCell> frames{};
        app::ScreenshotQueue& screenshots;
        infra::OptRef<const app::SectorRouteCell> sector_route{};
        infra::OptRef<const app::OsdRouteCell> osd_route{};
        infra::OptRef<const app::SysRouteCell> sys_route{};
        infra::OptRef<xthread::MainLogLane> main_log{};
    };

    struct Sources {
        infra::OptRef<const app::CmdFifoCell> fifo{};
        infra::OptRef<const app::MglPumpCell> mgl{};
        const Main<std::uint32_t>& mgl_row0;
        const Main<app::LoadWindowCounts>& window_counts;
        const app::BayCountsCell& bay_counts;
        const app::VideoStatsCell& video_stats;
        const app::VideoGeometryCell& video_geometry;
        app::VideoWire& video_wire;
        const app::DiagCountersCell& diag;
        const Main<std::uint32_t>& pause_expiries;
        const Main<std::uint32_t>& recover_polls;
        const Main<std::uint32_t>& save_write_failures;
        const Main<app::FallbackCounts>& fallbacks;
        const Main<std::uint32_t>& ini_write_failures;
        const app::DoorbellStatsCell& doorbell;
        const reactor::RoundTimingCell& round_timing;
        infra::OptRef<const reactor::FrameCell> frames{};
        infra::OptRef<const app::UiPageCell> ui_pages{};
        infra::OptRef<const app::HdOsdStatusCell> hd{};
        const app::ReplayStatusCell& replay;
        const app::RecorderStatusCell& rec_capture;
        const app::EncodeStatusCell& rec_encode;
        const app::RecWriteStatusCell& rec_write;
        const app::AviWriteStatusCell& rec_avi;
        app::ScreenshotQueue& screenshots;
        infra::OptRef<const app::SectorRouteCell> sector_route{};
        infra::OptRef<const app::OsdRouteCell> osd_route{};
        infra::OptRef<const app::SysRouteCell> sys_route{};
        infra::OptRef<xthread::MainLogLane> main_log{};
    };

    struct Services {
        infra::OptRef<app::IFrameSource> frame_source{};
        infra::OptRef<const app::InputSample> input_sample{};
        infra::OptRef<app::InputBuild> input_build{};
        infra::OptRef<svc::ChdPrefetch> prefetch{};
    };

    DiagSampler(
        xthread::RtStats& stats,
        app::EventQueue<SeatTag::RT>& events,
        const Main<RtEvidence>& ev,
        const xthread::WakeFlag& quiescing,
        const xthread::Telemetry<bool, SeatTag::RT>& transitioning,
        xthread::DiagLog& diag,
        xthread::LogLane& rt_lane,
        const Sources& sources,
        const Services& services
    ) noexcept;

    DiagSampler(const DiagSampler&) = delete;
    DiagSampler& operator=(const DiagSampler&) = delete;

    bool open_diag(const char* path) noexcept { return diag_.open(path); }

    const xthread::DiagLog& diag() const noexcept { return diag_; }

    xthread::DiagLog& diag_sink() noexcept { return diag_; }

    xthread::LogLane* rt_log_lane() noexcept { return &lane_rt_; }

    void set_screenshot_dir(std::string dir) noexcept { shot_dir_ = std::move(dir); }

    void drain_screenshots() noexcept;

    std::uint32_t screenshots_written() const noexcept { return shots_written_; }

    std::uint32_t screenshots_failed() const noexcept { return shots_failed_; }

    void set_sample_period_ms(unsigned ms) noexcept { sample_ms_ = ms; }

    [[nodiscard]] unsigned sample_period_ms() const noexcept { return sample_ms_; }

    const DiagStats& stats() const noexcept { return diag_stats_; }

    static constexpr std::size_t kRtstatsWorstCase = 4047;

    static constexpr std::size_t kSessWorstCase = infra::json_record_worst(
        SessRecord::kKey,
        infra::json_worst_len<SessView>()
    );

    static constexpr std::size_t kDiagLineCeiling = xthread::DiagLog::kMaxLine - 1;

    void sample(bool stopping) noexcept;

    void answer() noexcept;

    void render_final() noexcept;

    [[nodiscard]] std::uint64_t rss_ceiling_now() const noexcept;

    void observe_rss(std::uint64_t rss, std::uint64_t ceiling) noexcept;
    static constexpr unsigned kRssOverSamples = 2;

    void on(const LogRec::DeadlineMiss& a, const LogRec::Head& h) noexcept;
    void on(const LogRec::SessionTransition& a, const LogRec::Head& h) noexcept;
    void on(const LogRec::StagingRung& a, const LogRec::Head& h) noexcept;
    void on(const LogRec::CoreLoad& a, const LogRec::Head& h) noexcept;
    void on(const LogRec::CoreUnload& a, const LogRec::Head& h) noexcept;
    void on(const LogRec::Mount& a, const LogRec::Head& h) noexcept;
    void on(const LogRec::Unmount& a, const LogRec::Head& h) noexcept;
    void on(const LogRec::Refusal& a, const LogRec::Head& h) noexcept;
    void on(const LogRec::RingLoss& a, const LogRec::Head& h) noexcept;
    void on(const LogRec::DoorbellBound& a, const LogRec::Head& h) noexcept;
    void on(const LogRec::DoorbellPolling& a, const LogRec::Head& h) noexcept;
    void on(const LogRec::PhaseTolerated& a, const LogRec::Head& h) noexcept;
    void on(const LogRec::ConfStrArrived& a, const LogRec::Head& h) noexcept;
    void misrouted(const LogRec& r) noexcept;

    [[nodiscard]] std::uint32_t log_misrouted() const noexcept { return log_misrouted_; }

private:
    struct CdInstruments {
        std::uint32_t deadline_misses = 0;
        std::uint32_t deadline_slots = 0;
        std::uint32_t prefetch_state = 0;
        std::uint32_t hits = 0, misses = 0, decodes = 0;
        std::uint32_t seeks = 0, drops = 0, errors = 0;
        std::uint32_t decode_us = 0, evictions = 0;
    };

    CdInstruments cd_instruments() const noexcept;

    void render_records(const char* reason) noexcept;

    void render_rtstats(const char* reason, const app::DiagCounters& dc) noexcept;

    template <DiagRecord R>
    void emit_(const R& r, const RecordCtx& ctx) noexcept;

    void render_log_rec(const LogRec& r) noexcept;
    void line_(LogRec::Kind k, const LogRec::Head& h, std::uint32_t a, std::uint32_t b) noexcept;

    void drain_log_lane() noexcept;

    using LossCounts = std::array<std::uint32_t, infra::ordinal(LogRec::Kind::kCount)>;
    void voice_log_losses_(const LossCounts& losses, LossCounts& last, const char* src) noexcept;

    std::uint64_t rt_evidence_mask() const noexcept;
    unsigned rt_name_mask() const noexcept;
    unsigned rt_first_errno() const noexcept;

    xthread::RtStats& stats_;
    app::EventQueue<SeatTag::RT>& events_;

    const Main<RtEvidence>& ev_cell_;
    Main<RtEvidence>::Reader ev_reader_{};

    RtEvidence ev_{};
    const xthread::WakeFlag& quiescing_;
    const xthread::Telemetry<bool, SeatTag::RT>& transitioning_;

    app::VideoWire& video_wire_;
    infra::OptRef<app::InputBuild> input_build_{};
    infra::OptRef<const app::InputSample> input_{};
    infra::OptRef<svc::ChdPrefetch> prefetch_{};

    infra::OptRef<const reactor::FrameCell> frames_{};
    app::ScreenshotQueue& shots_;

    infra::OptRef<app::IFrameSource> frame_src_{};
    std::string shot_dir_;
    std::uint32_t shots_written_ = 0;
    std::uint32_t shots_failed_ = 0;
    infra::OptRef<const app::CmdFifoCell> fifo_stats_{};
    infra::OptRef<const app::MglPumpCell> mgl_stats_{};
    const xthread::Telemetry<std::uint32_t, SeatTag::Main>& mgl_row0_;
    const xthread::Telemetry<app::LoadWindowCounts, SeatTag::Main>& window_counts_;
    const app::VideoStatsCell& video_stats_;
    const app::VideoGeometryCell& video_geo_;
    const app::DiagCountersCell& diag_cell_;
    const app::RecorderStatusCell& rec_capture_;
    const app::EncodeStatusCell& rec_encode_;
    const app::DoorbellStatsCell& doorbell_cell_;
    const reactor::RoundTimingCell& round_timing_;
    infra::OptRef<const app::UiPageCell> ui_pages_{};
    xthread::DiagLog& diag_;
    xthread::LogLane& lane_rt_;
    xthread::LogLane::Consumer log_rx_;
    infra::OptRef<xthread::MainLogLane> lane_main_{};
    xthread::MainLogLane::Consumer main_rx_;

    LossCounts last_log_loss_{};
    LossCounts last_main_loss_{};
    std::uint32_t log_misrouted_ = 0;
    std::uint32_t last_log_misrouted_ = 0;
    std::uint32_t diag_seq_ = 0;

    unsigned unchanged_streak_ = 0;
    std::uint32_t last_dump_ = 0;
    std::uint32_t last_ev_loss_[app::EventQueue<SeatTag::RT>::kKinds] = {};
    std::uint64_t heartbeat_prev_ = 0;
    std::uint32_t last_hb_stalls_ = 0;
    std::uint32_t last_rss_over_ = 0;
    unsigned rss_over_streak_ = 0;
    std::uint32_t last_spin_ = 0;
    std::uint32_t last_ring_[kNumRings] = {};
    std::uint32_t last_dlm_ = 0;
    DiagRecords records_;
    DiagStats diag_stats_{};
    unsigned sample_ms_ = 1000;
};

}
