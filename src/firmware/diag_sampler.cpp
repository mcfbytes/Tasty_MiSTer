// SPDX-License-Identifier: GPL-3.0-or-later
#include "diag_sampler.h"

#include "assembly.h"
#include "rt_evidence.h"
#include "infra/seat.h"
#include "rtstats_format.h"

#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <ctime>
#include <optional>
#include <tuple>

#include "app/event.h"
#include "infra/log_rec_dispatch.h"
#include "infra/message_sum.h"
#include "app/frame_source.h"
#include "app/input_pipeline.h"
#include "app/screenshot_queue.h"
#include "svc/chd_prefetch.h"

namespace mister::fw {

namespace {

std::uint64_t mono_now_ns() noexcept {
    timespec ts{};
    (void)::clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<std::uint64_t>(ts.tv_sec) * 1'000'000'000ull +
           static_cast<std::uint64_t>(ts.tv_nsec);
}

DiagRecords make_records(const DiagSampler::Sources& sources) noexcept {
    return DiagRecords{
        SessRecord{sources.pause_expiries, sources.recover_polls, sources.save_write_failures,
                   sources.fallbacks},
        RndRecord{sources.round_timing}, TasRecord{sources.replay},
        RecRecord{sources.rec_capture, sources.rec_encode, sources.rec_write, sources.rec_avi},
        HdRecord{sources.hd}};
}

}  // namespace

DiagSampler::DiagSampler(xthread::RtStats& stats, app::EventQueue& events, const RtEvidence& ev,
                         const xthread::WakeFlag& quiescing, const std::atomic<bool>& transitioning,
                         xthread::DiagLog& diag, xthread::LogLane& rt_lane,
                         const Sources& sources) noexcept
    : stats_(stats), events_(events), ev_(ev), quiescing_(quiescing), transitioning_(transitioning),
      video_wire_(sources.video_wire), frames_(sources.frames), shots_(sources.screenshots),
      fifo_stats_(sources.fifo), mgl_stats_(sources.mgl), mgl_row0_(sources.mgl_row0),
      window_counts_(sources.window_counts), video_stats_(sources.video_stats),
      video_geo_(sources.video_geometry), diag_cell_(sources.diag),
      doorbell_cell_(sources.doorbell), round_timing_(sources.round_timing),
      ui_pages_(sources.ui_pages), diag_(diag), lane_rt_(rt_lane), records_(make_records(sources)) {
}

void DiagSampler::sample(bool stopping) noexcept {

    const std::uint64_t hb = stats_.heartbeat.load(std::memory_order_relaxed);
    const std::uint64_t prev = diag_stats_.heartbeat_last.exchange(hb, std::memory_order_relaxed);

    if (!stopping && !quiescing_.ever_requested() &&
        !transitioning_.load(std::memory_order_acquire) && hb != 0 && hb == prev) {
        if (++unchanged_streak_ >= 2) {
            diag_stats_.heartbeat_stalls.fetch_add(1, std::memory_order_relaxed);
            std::fprintf(stderr, "%s: executive heartbeat stalled at %llu\n", seat_name(kSeat),
                         static_cast<unsigned long long>(hb));
        }
    } else {
        unchanged_streak_ = 0;
    }

    if (auto rss = read_vm_rss_bytes()) {
        diag_stats_.rss_bytes.store(*rss, std::memory_order_relaxed);
        const bool over = *rss > kRssSoftCeilingBytes;

        if (over && !rss_over_now_) {
            diag_stats_.rss_over_ceiling.fetch_add(1, std::memory_order_relaxed);
            std::fprintf(stderr, "%s: RSS %llu over soft ceiling %llu\n", seat_name(kSeat),
                         static_cast<unsigned long long>(*rss),
                         static_cast<unsigned long long>(kRssSoftCeilingBytes));
        }
        rss_over_now_ = over;
    }

    if (prefetch_ != nullptr) {
        stats_.ring_drops[2].set(prefetch_->counters().drops);
    }

    stats_.ring_drops[3].set(lane_rt_.total_losses());

    diag_stats_.drains.fetch_add(1, std::memory_order_relaxed);

    if (diag_.is_open()) {

        for (std::size_t k = 0; k < app::EventQueue::kKinds; ++k) {
            const std::uint32_t l = events_.losses(static_cast<app::Event::Kind>(k));
            if (l != last_ev_loss_[k]) {
                diag_.appendf("{\"t\":\"drops\",\"src\":\"event\","
                              "\"kind\":%u,\"n\":%u}",
                              static_cast<unsigned>(k), l);
                last_ev_loss_[k] = l;
            }
        }

        drain_log_lane();

        bool alarm = false;
        const std::uint32_t hs = diag_stats_.heartbeat_stalls.load(std::memory_order_relaxed);
        const std::uint32_t ro = diag_stats_.rss_over_ceiling.load(std::memory_order_relaxed);
        if (hs != last_hb_stalls_) {
            last_hb_stalls_ = hs;
            alarm = true;
        }
        if (ro != last_rss_over_) {
            last_rss_over_ = ro;
            alarm = true;
        }
        if (stats_.spin_timeouts.get() != last_spin_) {
            last_spin_ = stats_.spin_timeouts.get();
            alarm = true;
        }
        for (std::size_t r = 0; r < kNumRings; ++r) {
            if (stats_.ring_drops[r].get() != last_ring_[r]) {
                last_ring_[r] = stats_.ring_drops[r].get();
                alarm = true;
            }
        }

        std::uint32_t dlm = 0;
        for (std::size_t i = 0; i < kMaxServices; ++i) {
            const std::uint32_t n = stats_.deadline_miss[i].get();
            dlm = (dlm > 0xFFFFFFFFu - n) ? 0xFFFFFFFFu : dlm + n;
        }
        if (dlm != last_dlm_) {
            last_dlm_ = dlm;
            alarm = true;
        }
        if (alarm) render_records("alarm");

        if (const std::uint32_t d = stats_.dump_requests.load(std::memory_order_relaxed);
            d != last_dump_) {
            last_dump_ = d;
            render_records("cmd");
        }
    }

    if (input_build_ != nullptr) (void)input_build_->rebind_round();

    drain_screenshots();
}

void DiagSampler::answer() noexcept {
    if (diag_.is_open()) {
        if (const std::uint32_t d = stats_.dump_requests.load(std::memory_order_relaxed);
            d != last_dump_) {
            last_dump_ = d;
            render_records("cmd");
        }
    }
    drain_screenshots();
}

void DiagSampler::render_final() noexcept {
    if (diag_.is_open()) render_records("final");
}

void DiagSampler::drain_screenshots() noexcept {
    while (auto req = shots_.take()) {
        bool ok = false;
        if (frame_src_ != nullptr) {
            if (auto frame = frame_src_->capture(req->scaled != 0); frame) {
                const auto png = app::encode_png_rgb24(frame->width, frame->height, frame->rgb);
                if (!png.empty()) {
                    std::string path;

                    const std::string_view name = req->path.view();
                    const std::size_t cut = name.rfind('/');
                    const std::string_view base =
                        cut == std::string_view::npos ? name : name.substr(cut + 1);
                    if (!shot_dir_.empty() && base != "." && base != "..") {

                        (void)::mkdir(shot_dir_.c_str(), 0777);
                        path = shot_dir_ + "/";
                        if (base.empty()) {
                            char stamp[32];
                            std::time_t t = std::time(nullptr);
                            std::tm tm{};
                            (void)::localtime_r(&t, &tm);
                            (void)std::strftime(stamp, sizeof stamp, "%Y%m%d_%H%M%S.png", &tm);
                            path += stamp;
                        } else {
                            path.append(base);
                        }
                        if (std::FILE* f = std::fopen(path.c_str(), "wb"); f != nullptr) {
                            ok = std::fwrite(png.data(), 1, png.size(), f) == png.size();
                            if (std::fclose(f) != 0) ok = false;
                        }
                    }
                }
            }
        }
        if (ok) {
            ++shots_written_;
        } else {
            ++shots_failed_;
        }
        req->ok = static_cast<std::uint8_t>(ok ? 1 : 0);
        req.complete();
    }
}

void DiagSampler::line_(LogRec::Kind k, const LogRec::Head& h, std::uint32_t a,
                        std::uint32_t b) noexcept {
    diag_.appendf("{\"t\":\"log\",\"k\":%u,\"sev\":%u,\"ts\":%llu,\"a\":%u,\"b\":%u}",
                  static_cast<unsigned>(k), static_cast<unsigned>(h.sev),
                  static_cast<unsigned long long>(h.t_ns), a, b);
}

void DiagSampler::render_log_rec(const LogRec& r) noexcept {
    infra::dispatch<LogRecRoutes>(r, *this);
}

void DiagSampler::on(const LogRec::DeadlineMiss& a, const LogRec::Head& h) noexcept {
    line_(a.kKind, h, a.over_ns, a.slot);
}
void DiagSampler::on(const LogRec::SessionTransition& a, const LogRec::Head& h) noexcept {
    line_(a.kKind, h, a.dwell_us, (static_cast<std::uint32_t>(a.from) << 8) | a.to);
}
void DiagSampler::on(const LogRec::StagingRung& a, const LogRec::Head& h) noexcept {
    line_(a.kKind, h, a.rung_us, a.rung_pc);
}
void DiagSampler::on(const LogRec::CoreLoad& a, const LogRec::Head& h) noexcept {
    line_(a.kKind, h, a.load_ms, 0);
}
void DiagSampler::on(const LogRec::CoreUnload& a, const LogRec::Head& h) noexcept {
    line_(a.kKind, h, a.detach_ms, 0);
}
void DiagSampler::on(const LogRec::Mount& a, const LogRec::Head& h) noexcept {
    line_(a.kKind, h, 0, a.slot);
}
void DiagSampler::on(const LogRec::Unmount& a, const LogRec::Head& h) noexcept {
    line_(a.kKind, h, 0, a.slot);
}
void DiagSampler::on(const LogRec::Refusal& a, const LogRec::Head& h) noexcept {
    line_(a.kKind, h, static_cast<std::uint32_t>(a.code), a.site);
}
void DiagSampler::on(const LogRec::RingLoss& a, const LogRec::Head& h) noexcept {
    line_(a.kKind, h, a.losses, static_cast<std::uint32_t>(infra::ordinal(a.lost)));
}
void DiagSampler::on(const LogRec::DoorbellBound& a, const LogRec::Head& h) noexcept {
    line_(a.kKind, h, a.cause_base, (static_cast<std::uint32_t>(a.klass) << 8) | a.line);
}
void DiagSampler::on(const LogRec::DoorbellPolling& a, const LogRec::Head& h) noexcept {
    line_(a.kKind, h, static_cast<std::uint32_t>(a.why),
          (static_cast<std::uint32_t>(a.klass) << 8) | a.line);
}
static_assert(LogRecSink<DiagSampler>);

void DiagSampler::misrouted(const LogRec&) noexcept { ++log_misrouted_; }

void DiagSampler::drain_log_lane() noexcept {
    for (std::size_t n = 0; n < xthread::kLogLaneCap; ++n) {
        const std::optional<LogRec> r = lane_rt_.pop();
        if (!r) break;
        render_log_rec(*r);
    }
    for (std::size_t k = 0; k < infra::ordinal(LogRec::Kind::kCount); ++k) {
        const auto lost = static_cast<LogRec::Kind>(k);
        const std::uint32_t l = lane_rt_.losses(lost);
        if (l == last_log_loss_[k]) continue;
        last_log_loss_[k] = l;

        render_log_rec(infra::make<LogRec>(
            LogRec::RingLoss{.losses = l, .lost = lost},
            LogRec::Head{xthread::sev_of(LogRec::Kind::RingLoss), {}, mono_now_ns()}));
        diag_.appendf("{\"t\":\"drops\",\"src\":\"log\",\"kind\":%u,\"n\":%u}",
                      static_cast<unsigned>(k), l);
    }

    if (log_misrouted_ != last_log_misrouted_) {
        last_log_misrouted_ = log_misrouted_;
        diag_.appendf("{\"t\":\"drops\",\"src\":\"log_misrouted\",\"n\":%u}", log_misrouted_);
    }
}

DiagSampler::CdInstruments DiagSampler::cd_instruments() const noexcept {
    CdInstruments c{};
    for (std::size_t i = 0; i < kMaxServices; ++i) {
        const std::uint32_t n = stats_.deadline_miss[i].get();
        if (n == 0) continue;
        c.deadline_slots |= (1u << i);

        if (c.deadline_misses > 0xFFFFFFFFu - n)
            c.deadline_misses = 0xFFFFFFFFu;
        else
            c.deadline_misses += n;
    }
    if (prefetch_ != nullptr) {
        const svc::PrefetchCounters p = prefetch_->counters();
        c.prefetch_state = p.state;
        c.hits = p.hits;
        c.misses = p.misses;
        c.decodes = p.decodes;
        c.seeks = p.seeks;
        c.drops = p.drops;
        c.errors = p.errors;
        c.decode_us = p.decode_us;
        c.evictions = p.evictions;
    }
    return c;
}

std::uint64_t DiagSampler::rt_evidence_mask() const noexcept { return fw::rt_evidence_mask(ev_); }

unsigned DiagSampler::rt_name_mask() const noexcept {
    static_assert(hal::kThreadSeats <= 32, "rt.nm is one 32-bit word");
    unsigned m = 0;
    for (std::size_t i = 0; i < hal::kThreadSeats; ++i) {
        if (ev_.seat_name[i].applied) m |= (1u << i);
    }
    return m;
}

unsigned DiagSampler::rt_first_errno() const noexcept { return fw::rt_first_errno(ev_); }

void DiagSampler::render_records(const char* reason) noexcept {

    app::DiagCounters dc = diag_cell_.sample().value;

    {
        const app::LoadWindowCounts w = window_counts_.sample().value;
        dc.file_tx_window = w.windows;
        dc.file_tx_window_refusals = w.refusals;
        dc.file_tx_window_refusal_code = w.refusal_code;
    }
    render_rtstats(reason, dc);
    const RecordCtx ctx{dc};
    std::apply([&](const auto&... r) { (emit_(r, ctx), ...); }, records_);
    ++diag_seq_;
}

template <DiagRecord R>
void DiagSampler::emit_(const R& r, const RecordCtx& ctx) noexcept {
    static_assert(infra::json_record_worst(R::kKey, infra::json_worst_len<typename R::View>()) <=
                      kDiagLineCeiling,
                  "a record's worst case must fit the xthread::DiagLog ceiling");
    const std::optional<typename R::View> v = r.sample(ctx);
    if (!v) return;
    infra::JsonOut o;
    o.begin_record(R::kKey, diag_seq_);
    to_json(o, *v);
    o.end_record();
    diag_.append(o);
}

void DiagSampler::render_rtstats(const char* reason, const app::DiagCounters& dc) noexcept {

    const app::DoorbellStats db = doorbell_cell_.sample().value;
    const std::uint32_t ev_lost_edge = events_.losses(app::Delivery::Edge);
    const std::uint32_t ev_lost = events_.losses(app::Delivery::Hint) + ev_lost_edge;
    const app::CmdFifoStats fs =
        (fifo_stats_ != nullptr) ? fifo_stats_->sample().value : app::CmdFifoStats{};
    const CdInstruments cd = cd_instruments();
    const app::IFrameSource::Counts shot =
        (frame_src_ != nullptr) ? frame_src_->counts() : app::IFrameSource::Counts{};

    const app::VideoPumpStats vs = video_stats_.sample().value;
    const app::VideoGeometryRecord gr = video_geo_.sample().value;
    const app::VideoWire::Stats ws = video_wire_.stats();
    const app::VideoWire::GeoStats gs = video_wire_.geo_stats();
    const app::VideoWire::PreludeStats ps = video_wire_.prelude_stats();

    const unsigned rf_bits =
        (vs.solve_failures != 0u ? 1u : 0u) | (vs.zero_divider != 0u ? 2u : 0u) |
        (vs.stage_failures != 0u ? 4u : 0u) | (ws.stale != 0u ? 8u : 0u) |
        (ws.partial != 0u ? 16u : 0u) | (ws.rejected != 0u ? 32u : 0u) |
        (vs.parse_fallbacks != 0u ? 64u : 0u) | (vs.empty_specs != 0u ? 128u : 0u) |
        (vs.fb_cmds_dropped != 0u ? 256u : 0u);

    const unsigned long long refused_wide =
        static_cast<unsigned long long>(vs.solve_failures) + vs.zero_divider + vs.stage_failures +
        static_cast<unsigned long long>(ws.stale) + ws.partial + ws.rejected + vs.empty_specs;
    const unsigned video_refused =
        refused_wide > 0xFFFFFFFFull ? 0xFFFFFFFFu : static_cast<unsigned>(refused_wide);

    static_assert(DiagSampler::kRtstatsWorstCase <= DiagSampler::kDiagLineCeiling,
                  "the rtstats record's worst case must fit the xthread::DiagLog "
                  "ceiling — raise kMaxLine in infra/diag_log.h or narrow a "
                  "field, never let the sink swap the record for a "
                  "{\"t\":\"trunc\"} marker");

    static_assert(reactor::round_bucket_edge_us(reactor::kRoundBuckets - 1) == 536871u);
    static_assert(static_cast<unsigned>(reactor::RoundSegment::kCount) - 1 == 4u);
    static_assert(kMaxServices - 1 == 23u);
    static_assert(reactor::kRoundLifecycleAll == 7u);

    const app::InputStats ips =
        app::collect_input_stats((input_ != nullptr) ? *input_ : app::InputSample{});
    const app::MglPumpStats ms =
        (mgl_stats_ != nullptr) ? mgl_stats_->sample().value : app::MglPumpStats{};
    const std::uint32_t mgl_row0 = mgl_row0_.sample().value;

    const app::UiPageRecord up =
        (ui_pages_ != nullptr) ? ui_pages_->sample().value : app::UiPageRecord{};

    const reactor::FrameRecord fr =
        (frames_ != nullptr) ? frames_->sample().value : reactor::FrameRecord{};

    const reactor::RoundTiming rnd = round_timing_.sample().value;

    diag_.appendf(
        TASTY_RTSTATS_FMT, diag_seq_, reason,
        static_cast<unsigned long long>(stats_.heartbeat.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(diag_stats_.drains.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(diag_stats_.rss_bytes.load(std::memory_order_relaxed)),
        diag_stats_.heartbeat_stalls.load(std::memory_order_relaxed),
        diag_stats_.rss_over_ceiling.load(std::memory_order_relaxed), stats_.ring_drops[0].get(),
        stats_.ring_drops[1].get(), stats_.ring_drops[2].get(), stats_.ring_drops[3].get(),
        stats_.spin_timeouts.get(), stats_.spurious_causes.get(),
        static_cast<unsigned>(events_.depth()), ev_lost, ev_lost_edge, fs.reads, fs.lines,
        fs.unrouted, fs.unrecognised, static_cast<unsigned>(dc.ladder_live),
        static_cast<unsigned>(dc.ladder_pc), dc.ladder_rungs,
        static_cast<unsigned long long>(dc.ladder_bytes), dc.ladder_assets,
        static_cast<unsigned>(dc.ladder_bios_found), static_cast<unsigned>(dc.ladder_disc_mounted),
        static_cast<unsigned>(dc.ladder_save_mounted), vs.edges, ws.staged, ws.emitted, ws.arm_fail,
        video_refused, rf_bits, static_cast<unsigned>(vs.last_mode_index), vs.last_fpix_khz,
        static_cast<unsigned>(ps.fb_ack), static_cast<unsigned>(gr.fb_en),
        static_cast<unsigned>(gr.valid), static_cast<unsigned>(gr.res), gr.width, gr.height,
        gs.samples, gs.changes, gs.resends, gs.errors, gs.late, gs.generation, gs.torn, ps.arms,
        ps.completions, ps.chunks, ps.coeff_words, ps.parked, ps.barrier_timeouts, ps.errors,
        static_cast<unsigned>(ps.flt_flags), static_cast<unsigned>(ps.hdmi_int),
        static_cast<unsigned>(ps.gamma_cap), static_cast<unsigned>(ps.af_flags), ps.afilter_skips,
        static_cast<unsigned>(ps.tail_fail), static_cast<unsigned>(ps.step),
        app::VideoPump::i2c_outcome_name(vs.i2c), static_cast<unsigned>(vs.i2c_bus), vs.i2c_writes,
        vs.i2c_errors, static_cast<unsigned>(vs.i2c_fail_reg),
        static_cast<unsigned>(vs.i2c_first_op), static_cast<unsigned>(vs.i2c_first_reg),
        static_cast<unsigned>(vs.i2c_first_errno), static_cast<unsigned>(vs.i2c_last_errno),
        vs.i2c_verify_errors, vs.i2c_poll_errors, vs.i2c_reprobes, vs.i2c_trips,
        static_cast<unsigned>(vs.i2c_breaker_open), vs.spd_sends, vs.spd_disables, vs.spd_skips_dv,
        vs.spd_noname, static_cast<unsigned>(vs.spd_fail_reg), ips.rounds, ips.events,
        ips.keys_emitted, ips.keys_dropped, ips.key_sweeps, ips.keys_released, ips.mouse_packets,
        ips.joy_transactions, ips.kicks, ips.rebuilds_done, ips.rebuilds_refused, ips.fd_evictions,
        ips.enumerate_failures, ips.rt_errors, ips.devices, ips.registered, ips.mapped, ips.slotted,
        ips.gates, ips.mask_changes, ips.joy_seen, ips.joy_players, ips.axis_edges,
        ips.mouse_remainder, ips.key_overflows, ips.ui_keys, ips.ui_keys_dropped, ips.quirk_drops,
        ips.rejected, ips.button_samples, ips.button_actions, ips.slot_live, ips.slot_ghosts,
        ips.slot_hash[0], ips.slot_hash[1], ips.slot_hash[2], ips.slot_hash[3], ips.slot_hash[4],
        ips.slot_hash[5], ms.file_items, ms.abandoned, ms.armed, ms.publishes, mgl_row0,
        dc.file_tx_count, static_cast<unsigned long long>(dc.file_tx_bytes),
        static_cast<unsigned>(dc.file_tx_index), dc.file_tx_window, dc.file_tx_window_refusals,
        static_cast<unsigned>(dc.file_tx_window_refusal_code),

        dc.blk_rounds, dc.blk_served, dc.blk_errors, static_cast<unsigned>(dc.blk_err_code),
        dc.blk_oversize, dc.blk_unencodable, dc.blk_blank_filled, dc.blk_write_failures,
        dc.blk_stock, dc.blk_config, dc.blk_discarded, dc.blk_deferred, dc.blk_expired,
        dc.blk_blocks, dc.blk_pf_expired, dc.blk_staging_expired, dc.blk_stale,

        db.declared, db.bound, db.refusals, db.fallbacks, db.retirements,
        static_cast<unsigned>(ev_.online_cpus < 0 ? 0 : ev_.online_cpus),
        static_cast<unsigned>(ev_.cpu_coverage.applied),
        static_cast<unsigned long long>(rt_evidence_mask()), rt_name_mask(), rt_first_errno(),

        rnd.epoch, reactor::round_us(rnd.epoch_ns / 1000), rnd.steady.n,
        reactor::round_us(rnd.steady.min_ns), reactor::round_us(rnd.steady.max_ns),
        reactor::round_p99_us(rnd.steady), rnd.steady.over_1ms,
        static_cast<unsigned>(rnd.steady.max_cause.seg),
        static_cast<unsigned>(rnd.steady.max_cause.index), rnd.lifecycle.n,
        reactor::round_us(rnd.lifecycle.max_ns), reactor::round_p99_us(rnd.lifecycle),
        static_cast<unsigned>(rnd.lifecycle.max_cause.seg),
        static_cast<unsigned>(rnd.lifecycle.max_cause.index),
        static_cast<unsigned>(rnd.lifecycle.max_cause.lifecycle), rnd.wake.n,
        reactor::round_avg_us(rnd.wake), reactor::round_us(rnd.wake.max_ns),
        reactor::round_p99_us(rnd.wake), rnd.tick_overruns, rnd.all_steady_rounds,
        reactor::round_us(rnd.all_steady_max_ns), static_cast<unsigned>(rnd.all_steady_cause.seg),
        static_cast<unsigned>(rnd.all_steady_cause.index), rnd.steady_max_epoch,
        rnd.all_steady_over_1ms, rnd.all_life_rounds, reactor::round_us(rnd.all_life_max_ns),
        static_cast<unsigned>(rnd.all_life_cause.seg),
        static_cast<unsigned>(rnd.all_life_cause.index),
        static_cast<unsigned>(rnd.all_life_cause.lifecycle), reactor::round_us(rnd.all_wake_max_ns),
        cd.deadline_misses, cd.deadline_slots, cd.prefetch_state, cd.hits, cd.misses, cd.decodes,
        cd.seeks, cd.drops, cd.errors, dc.disc_sync_decompress, dc.disc_park_timeouts,
        dc.disc_prefetch_refusals,

        dc.cd_flow_waits, dc.cd_cdda_sectors,

        dc.cd_data_sectors, dc.cd_idle_ticks, dc.cd_gated_ticks,
        static_cast<unsigned>(dc.cd_drive_state), dc.cd_drive_track,
        static_cast<int>(dc.cd_drive_lba), static_cast<unsigned>(dc.cd_drive_is_data),
        static_cast<int>(dc.cd_drive_audio_lba), dc.cd_substitutes, dc.cd_subcode_substitutes,
        dc.cd_not_resident, cd.decode_us, cd.evictions, dc.cd_busy_ticks, dc.cd_egress_abandons,

        up.depth, up.top_id, up.vis_publishes, up.cell_refusals, up.page_refusals, fr.seq,
        shots_.drops(), shots_.result_drops(), shots_written_, shots_failed_,

        shot.captures, shot.refused, shot.torn, diag_.drops(), diag_.truncations());
}

}  // namespace mister::fw
