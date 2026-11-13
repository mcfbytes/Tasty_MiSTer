// SPDX-License-Identifier: GPL-3.0-or-later
#include "diag_sampler.h"

#include "infra/wired_end.h"
#include "rt_evidence.h"
#include "infra/seat.h"
#include "round_trace_record.h"
#include "rtstats_format.h"
#include "os/vm_status.h"

#include <sys/stat.h>
#include <unistd.h>

#include <concepts>
#include <cstdio>
#include <ctime>
#include <limits>
#include <optional>
#include <tuple>
#include <utility>

#include "app/event.h"
#include "app/log_clock.h"
#include "infra/log_rec_dispatch.h"
#include "infra/message_sum.h"
#include "infra/path_parts.h"
#include "app/frame_source.h"
#include "app/input_pipeline.h"
#include "app/screenshot_queue.h"
#include "svc/chd_prefetch.h"
#include "svc/input_service_counts.h"

namespace mister::fw {

using infra::wired_end;

namespace {

using Ring = xthread::RtStats::Ring;

template <std::same_as<std::uint8_t>... Bytes>
[[nodiscard]] constexpr std::uint32_t pack_bytes(Bytes... bytes) noexcept {
    constexpr unsigned kByteBits = std::numeric_limits<std::uint8_t>::digits;
    std::uint32_t word = 0;
    ((word = (word << kByteBits) | bytes), ...);
    return word;
}

DiagRecords make_records(const DiagSampler::Sources& sources) noexcept {
    const DiagSampler::DeviceCells& d = sources.device;
    return DiagRecords{
        SessRecord{d.sess},
        BayRecord{
            d.bay_counts,
            d.sector_route,
            d.osd_route,
            d.sys_route,
            d.sector_lease,
            d.osd_lease,
            d.sys_lease
        },
        RndRecord{d.round_timing},
        TasRecord{sources.replay},
        RecRecord{sources.rec_capture, sources.rec_encode, sources.rec_write, sources.rec_avi},
        HdRecord{sources.hd},
        RgRecord{sources.ui_pages}
    };
}

[[nodiscard]] constexpr std::chrono::duration<std::uint32_t, std::milli> epoch_ms_of(
    const reactor::RoundTiming& t
) noexcept {
    const std::uint64_t ms = std::chrono::floor<std::chrono::duration<std::uint64_t, std::milli>>(
                                 std::chrono::duration<std::uint64_t, std::nano>{t.epoch_ns}
    )
                                 .count();
    return std::chrono::duration<std::uint32_t, std::milli>{app::sat_u32(ms)};
}

static_assert(
    epoch_ms_of(reactor::RoundTiming{.epoch_ns = 1'000'000}).count() == 1,
    "a millisecond is one millisecond, not a raw nano count"
);
static_assert(
    epoch_ms_of(reactor::RoundTiming{.epoch_ns = std::numeric_limits<std::uint64_t>::max()}).count()
        == std::numeric_limits<std::uint32_t>::max(),
    "the record's field saturates, as the %u slot requires"
);

}

DiagSampler::DiagSampler(
    const Wiring& wiring,
    const Sources& sources,
    const Services& services
) noexcept
    : stats_(wiring.stats),
      events_(wiring.events),
      ev_cell_(wiring.ev),
      quiescing_(wiring.quiescing),
      transitioning_(wiring.transitioning),
      video_wire_(sources.video_wire),
      prelude_stats_(sources.prelude_stats),
      input_build_(services.input_build),
      input_(services.input_sample),
      prefetch_(services.prefetch),
      frames_(sources.device.frames),
      shots_(sources.device.screenshots),
      frame_src_(services.frame_source),
      fifo_stats_(sources.fifo),
      mgl_stats_(sources.mgl),
      mgl_row0_(sources.device.mgl_row0),
      window_counts_(sources.device.window_counts),
      video_stats_(sources.video_stats),
      video_geo_(sources.video_geometry),
      diag_cell_(sources.device.diag),
      rec_capture_(sources.rec_capture),
      rec_encode_(sources.rec_encode),
      doorbell_cell_(sources.device.doorbell),
      round_timing_(sources.device.round_timing),
      round_traces_(sources.device.round_traces),
      trace_rx_(wired_end(
          sources.device.round_traces.consumer(),
          "T-DIAG: round trace lane consumer minted twice"
      )),
      ui_pages_(sources.ui_pages),
      diag_(wiring.diag),
      lane_rt_(wiring.rt_lane),
      log_rx_(wired_end(wiring.rt_lane.consumer(), "T-DIAG: RT log lane consumer minted twice")),
      lane_main_(sources.device.main_log),
      main_rx_(
          sources.device.main_log ? wired_end(
                                        sources.device.main_log->consumer(),
                                        "T-DIAG: MAIN log lane consumer minted twice"
                                    )
                                  : xthread::MainLogLane::Consumer{}
      ),
      records_(make_records(sources)) {}

std::uint64_t DiagSampler::rss_ceiling_now() const noexcept {
    app::RecorderStatus cap{};
    app::EncodeStatus enc{};
    const std::uint32_t cap_kib = rec_capture_.sample_into(cap) != 0 ? cap.arena_kib : 0;
    const std::uint32_t chunk_kib = rec_encode_.sample_into(enc) != 0 ? enc.video.arena_kib : 0;
    return rss_ceiling_bytes(cap_kib, chunk_kib);
}

void DiagSampler::observe_rss(RssSample reading) noexcept {
    diag_stats_.rss_bytes.publish(reading.rss_bytes);
    rss_over_streak_ = reading.over() ? rss_over_streak_ + 1 : 0;

    if (rss_over_streak_ == kRssOverSamples) {
        diag_stats_.rss_over_ceiling.add();
        std::fprintf(
            stderr,
            "memory use %s bytes is over the expected ceiling %s\n",
            reading.rss_bytes.text().c_str(),
            reading.ceiling_bytes.text().c_str()
        );
    }
}

void DiagSampler::sample(bool stopping) noexcept {

    const std::uint64_t hb = stats_.heartbeat.load(std::memory_order_relaxed);
    const std::uint64_t prev = heartbeat_prev_;
    diag_stats_.heartbeat_last.publish(hb);
    heartbeat_prev_ = hb;

    if (!stopping
        && !quiescing_.ever_requested()
        && !transitioning_.sample().value
        && hb != 0
        && hb == prev) {
        constexpr std::uint32_t kStallStreak = 2;
        if (++unchanged_streak_ >= kStallStreak) {
            diag_stats_.heartbeat_stalls.add();
            std::fprintf(
                stderr,
                "%s: executive heartbeat stalled at %llu\n",
                seat_name(kSeat),
                static_cast<unsigned long long>(hb)
            );
        }
    } else {
        unchanged_streak_ = 0;
    }

    const std::uint64_t ceiling = rss_ceiling_now();
    if (auto rss = os::read_vm_rss_bytes())
        observe_rss(
            RssSample{
                .rss_bytes = proto::FileSize::of(*rss),
                .ceiling_bytes = proto::FileSize{ceiling}
            }
        );

    if (prefetch_) {
        stats_.drops(Ring::prefetch).set(prefetch_->counters().drops);
    }

    stats_.drops(Ring::log).set(lane_rt_.losses().total());

    diag_stats_.drains.add();

    if (diag_.is_open()) {

        const auto ev_now = events_.losses();
        for (std::size_t k = 0; k < app::EventQueue<SeatTag::RT>::kKinds; ++k) {
            const std::uint32_t l = ev_now.per_kind[k];
            if (l != last_ev_loss_[k]) {
                diag_.appendf(
                    "{\"t\":\"drops\",\"src\":\"event\","
                    "\"kind\":%u,\"n\":%u}",
                    static_cast<unsigned>(k),
                    l
                );
                last_ev_loss_[k] = l;
            }
        }

        drain_log_lane();

        bool alarm = false;
        const std::uint32_t hs = diag_stats_.heartbeat_stalls.get();
        const std::uint32_t ro = diag_stats_.rss_over_ceiling.get();
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
        for (const Ring r : {Ring::cmd, Ring::event, Ring::prefetch, Ring::log}) {
            const std::size_t slot = infra::ordinal(r);
            const std::uint32_t n = stats_.drops(r).get();
            if (n != last_ring_[slot]) {
                last_ring_[slot] = n;
                alarm = true;
            }
        }

        std::uint32_t dlm = 0;
        for (const xthread::Counter& miss : stats_.deadline_miss) {
            dlm = app::sat_u32(std::uint64_t{dlm} + miss.get());
        }

        if (dlm > last_dlm_)
            alarm = true;
        last_dlm_ = dlm;
        if (alarm)
            render_records(RecordReason::Alarm);

        if (const std::uint32_t d = stats_.dump_requests.load(std::memory_order_relaxed);
            d != last_dump_) {
            last_dump_ = d;
            render_records(RecordReason::Cmd);
        }
    }

    if (input_build_)
        (void)input_build_->rebind_round();

    drain_screenshots();
}

void DiagSampler::answer() noexcept {
    if (diag_.is_open()) {
        drain_round_traces();
        if (const std::uint32_t d = stats_.dump_requests.load(std::memory_order_relaxed);
            d != last_dump_) {
            last_dump_ = d;
            render_records(RecordReason::Cmd);
        }
    }
    drain_screenshots();
}

void DiagSampler::render_final() noexcept {
    if (!diag_.is_open())
        return;
    drain_round_traces();
    voice_trace_drops_();
    render_records(RecordReason::Final);
}

void DiagSampler::drain_screenshots() noexcept {
    while (auto req = shots_.take()) {
        bool ok = false;
        if (frame_src_) {
            if (auto frame = frame_src_->capture(req->scaled != 0); frame) {
                const auto png = png_.encode(
                    app::PictureSize{
                        app::PicturePixels::of(frame->width),
                        app::PictureRows::of(frame->height)
                    },
                    frame->rgb
                );
                if (!png.empty()) {
                    std::string path;

                    const std::string_view name = req->path.view();
                    const std::string_view base = infra::basename(name);
                    if (!shot_dir_.empty() && base != "." && base != "..") {

                        (void)::mkdir(shot_dir_.c_str(), S_IRWXU | S_IRWXG | S_IRWXO);
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
                            if (std::fclose(f) != 0)
                                ok = false;
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
        req->ok = ok;
        req.complete();
    }
}

void DiagSampler::line_(
    LogRec::Kind k,
    const LogRec::Head& h,
    std::uint32_t a,
    std::uint32_t b
) noexcept {
    diag_.appendf(
        "{\"t\":\"log\",\"k\":%u,\"sev\":%u,\"ts\":%llu,\"a\":%u,\"b\":%u}",
        static_cast<unsigned>(k),
        static_cast<unsigned>(h.sev),
        static_cast<unsigned long long>(h.t.count()),
        a,
        b
    );
}

void DiagSampler::render_log_rec(const LogRec& r) noexcept {
    infra::dispatch<LogRecRoutes>(r, *this);
}

void DiagSampler::on(const LogRec::DeadlineMiss& a, const LogRec::Head& h) noexcept {
    line_(a.kKind, h, a.over.count(), a.slot.wire());
}

void DiagSampler::on(const LogRec::SessionTransition& a, const LogRec::Head& h) noexcept {
    line_(a.kKind, h, a.dwell.count(), pack_bytes(a.from, a.to));
}

void DiagSampler::on(const LogRec::StagingRung& a, const LogRec::Head& h) noexcept {
    line_(a.kKind, h, a.rung.count(), a.rung_pc);
}

void DiagSampler::on(const LogRec::CoreLoad& a, const LogRec::Head& h) noexcept {
    line_(a.kKind, h, a.load.count(), 0);
}

void DiagSampler::on(const LogRec::CoreUnload& a, const LogRec::Head& h) noexcept {
    line_(a.kKind, h, a.detach.count(), 0);
}

void DiagSampler::on(const LogRec::Mount& a, const LogRec::Head& h) noexcept {
    line_(a.kKind, h, 0, a.slot);
}

void DiagSampler::on(const LogRec::Unmount& a, const LogRec::Head& h) noexcept {
    line_(a.kKind, h, 0, a.slot);
}

void DiagSampler::on(const LogRec::Refusal& a, const LogRec::Head& h) noexcept {
    diag_.appendf(
        "{\"t\":\"log\",\"k\":%u,\"sev\":%u,\"ts\":%llu,\"a\":%u,\"b\":%u}",
        static_cast<unsigned>(a.kKind),
        static_cast<unsigned>(h.sev),
        static_cast<unsigned long long>(h.t.count()),
        static_cast<unsigned>(a.code),
        static_cast<unsigned>(a.site)
    );
}

void DiagSampler::on(const LogRec::RingLoss& a, const LogRec::Head& h) noexcept {
    line_(a.kKind, h, a.losses, app::sat_u32(infra::ordinal(a.lost)));
}

void DiagSampler::on(const LogRec::DoorbellBound& a, const LogRec::Head& h) noexcept {
    line_(a.kKind, h, a.cause_base, pack_bytes(a.klass, a.line));
}

void DiagSampler::on(const LogRec::DoorbellPolling& a, const LogRec::Head& h) noexcept {
    line_(a.kKind, h, errc_code(a.why), pack_bytes(a.klass, a.line));
}

void DiagSampler::on(const LogRec::PhaseTolerated& a, const LogRec::Head& h) noexcept {
    line_(a.kKind, h, a.stimulus, pack_bytes(a.phase, a.under));
}

void DiagSampler::on(const LogRec::ConfStrArrived& a, const LogRec::Head& h) noexcept {
    line_(a.kKind, h, a.bind_gen, pack_bytes(a.phase, a.under, std::uint8_t{a.awaited}));
}

static_assert(LogRecSink<DiagSampler>);

void DiagSampler::misrouted(const LogRec&) noexcept {
    TASTY_SEAT_BODY(DiagSampler);
    ++log_misrouted_;
}

void DiagSampler::drain_log_lane() noexcept {
    for (std::size_t n = 0; n < xthread::kLogLaneCap; ++n) {
        const std::optional<LogRec> r = log_rx_.pop();
        if (!r)
            break;
        render_log_rec(*r);
    }
    voice_log_losses_(
        loss_watch_[static_cast<std::size_t>(LogLaneId::Rt)].moved(lane_rt_.losses().per_kind),
        LogLaneId::Rt
    );
    if (lane_main_) {
        for (std::size_t n = 0; n < xthread::kMainLogLaneCap; ++n) {
            const std::optional<LogRec> r = main_rx_.pop();
            if (!r)
                break;
            render_log_rec(*r);
        }
        voice_log_losses_(
            loss_watch_[static_cast<std::size_t>(LogLaneId::Main)].moved(
                lane_main_->losses().per_kind
            ),
            LogLaneId::Main
        );
    }

    if (log_misrouted_ != last_log_misrouted_) {
        last_log_misrouted_ = log_misrouted_;
        diag_.appendf("{\"t\":\"drops\",\"src\":\"log_misrouted\",\"n\":%u}", log_misrouted_);
    }
}

void DiagSampler::drain_round_traces() noexcept {
    const reactor::MonoInstant now = std::chrono::steady_clock::now();
    if (now - trace_window_ >= kTraceWindow) {
        voice_trace_drops_();
        trace_window_ = now;
        trace_spent_ = {};
    }
    for (std::size_t n = 0; n < reactor::RoundTraceLane::kCapacity; ++n) {
        const std::optional<reactor::RoundTrace> r = trace_rx_.pop();
        if (!r)
            break;
        infra::dispatch<infra::AllRouted>(*r, *this);
    }
}

void DiagSampler::on(const reactor::RoundTrace::SlowSteady& a) noexcept {
    render_trace_(a.kKind, a.round);
}

void DiagSampler::on(const reactor::RoundTrace::SlowLifecycle& a) noexcept {
    render_trace_(a.kKind, a.round);
}

void DiagSampler::render_trace_(
    reactor::RoundTrace::Kind k,
    const reactor::RoundTrace::Round& r
) noexcept {
    TraceKindDrops& drops = trace_drops_[infra::ordinal(k)];
    if (trace_bytes_ >= kTraceBytesPerProcess) {
        drops.cap = drops.cap.added();
        return;
    }
    if (trace_spent_ >= kTraceLinesPerWindow) {
        drops.budget = drops.budget.added();
        return;
    }
    infra::JsonOut o;
    o.begin_record(RoundTraceRecord::kKey, diag_seq_);
    to_json(o, r);
    o.end_record();

    const TraceBytes after = trace_bytes_.plus(o.view().size() + 1);
    if (after > kTraceBytesPerProcess) {
        trace_bytes_ = kTraceBytesPerProcess;
        drops.cap = drops.cap.added();
        return;
    }
    trace_bytes_ = after;
    trace_spent_ = trace_spent_.next();
    diag_.append(o);
}

void DiagSampler::voice_trace_drops_() noexcept {
    const reactor::RoundTraceLane::Losses lost = round_traces_.losses();
    for (const reactor::RoundTrace::Kind k : kTraceKinds) {
        TraceKindDrops& drops = trace_drops_[infra::ordinal(k)];
        drops.lane.drops = lost.of(k);
        voice_trace_("rtflight", k, drops.lane);
        voice_trace_("rtflight_budget", k, drops.budget);
        voice_trace_("rtflight_cap", k, drops.cap);
        drops = {drops.lane.voiced(), drops.budget.voiced(), drops.cap.voiced()};
    }
    if (trace_misroutes_.moved()) {
        trace_misroutes_ = trace_misroutes_.voiced();
        diag_.appendf(
            "{\"t\":\"drops\",\"src\":\"rtflight_misrouted\",\"n\":%u}",
            trace_misroutes_.drops
        );
    }
}

void DiagSampler::voice_trace_(
    const char* src,
    reactor::RoundTrace::Kind k,
    TraceDrops d
) noexcept {
    if (d.moved())
        diag_.appendf(
            "{\"t\":\"drops\",\"src\":\"%s\",\"kind\":%u,\"n\":%u}",
            src,
            static_cast<unsigned>(infra::ordinal(k)),
            d.drops
        );
}

void DiagSampler::misrouted(const reactor::RoundTrace&) noexcept {
    TASTY_SEAT_BODY(DiagSampler);
    trace_misroutes_ = trace_misroutes_.added();
}

void DiagSampler::voice_log_losses_(const LossMoves& moved, LogLaneId lane) noexcept {
    for (std::size_t k = 0; k < moved.size(); ++k) {
        if (!moved[k])
            continue;
        const auto lost = static_cast<LogRec::Kind>(k);

        render_log_rec(
            infra::make<LogRec>(
                LogRec::RingLoss{.losses = *moved[k], .lost = lost},
                LogRec::Head{
                    xthread::sev_of(LogRec::Kind::RingLoss),
                    {},
                    app::log_stamp(app::log_now())
                }
            )
        );
        diag_.appendf(
            "{\"t\":\"drops\",\"src\":\"%s\",\"kind\":%u,\"n\":%u}",
            name(lane),
            static_cast<unsigned>(k),
            *moved[k]
        );
    }
}

DiagSampler::CdInstruments DiagSampler::cd_instruments() const noexcept {
    CdInstruments c{};
    for (ServiceSlot i{}; i.index() < kMaxServices; i = i.next()) {
        const std::uint32_t n = stats_.deadline_miss[i].get();
        if (n == 0)
            continue;
        c.deadline_slots |= (1u << i.wire());

        c.deadline_misses = app::sat_u32(std::uint64_t{c.deadline_misses} + n);
    }
    if (prefetch_) {
        const svc::PrefetchCounters p = prefetch_->counters();
        c.prefetch_state = std::to_underlying(p.state);
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

std::uint64_t DiagSampler::rt_evidence_mask() const noexcept {
    return fw::rt_evidence_mask(ev_);
}

unsigned DiagSampler::rt_name_mask() const noexcept {
    static_assert(hal::kThreadSeats <= 32, "rt.nm is one 32-bit word");
    unsigned m = 0;
    for (std::size_t i = 0; i < hal::kThreadSeats; ++i) {
        if (ev_.seats[i].name.applied)
            m |= (1u << i);
    }
    return m;
}

unsigned DiagSampler::rt_first_errno() const noexcept {
    return fw::rt_first_errno(ev_);
}

void DiagSampler::render_records(RecordReason reason) noexcept {

    if (auto fresh = ev_reader_.take_if_changed(ev_cell_))
        ev_ = *fresh;

    app::DiagCounters dc = diag_cell_.sample().value;

    {
        const app::LoadWindowCounts w = window_counts_.sample().value;
        dc.file_tx_window = w.windows;
        dc.file_tx_window_refusals = w.refusals;
        dc.file_tx_window_refusal_code = 0;
        if (w.refusal)
            dc.file_tx_window_refusal_code = errc_code(*w.refusal);
    }
    render_rtstats(reason, dc);
    const RecordCtx ctx{dc};
    std::apply([&](const auto&... r) { (emit_(r, ctx), ...); }, records_);
    ++diag_seq_;
}

template <DiagRecord R>
void DiagSampler::emit_(const R& r, const RecordCtx& ctx) noexcept {
    static_assert(
        infra::json_record_worst(R::kKey, infra::json_worst_len<typename R::View>())
            <= kDiagLineCeiling,
        "a record's worst case must fit the xthread::DiagLog ceiling"
    );
    const std::optional<typename R::View> v = r.sample(ctx);
    if (!v)
        return;
    infra::JsonOut o;
    o.begin_record(R::kKey, diag_seq_);
    to_json(o, *v);
    o.end_record();
    diag_.append(o);
}

void DiagSampler::render_rtstats(RecordReason reason, const app::DiagCounters& dc) noexcept {

    const app::DoorbellStats db = doorbell_cell_.sample().value;
    const std::uint32_t ev_lost_edge = app::delivery_losses(events_.losses(), app::Delivery::Edge);
    const std::uint32_t ev_lost = app::delivery_losses(events_.losses(), app::Delivery::Hint)
        + ev_lost_edge;
    const app::CmdFifoStats fs = (fifo_stats_) ? fifo_stats_->sample().value : app::CmdFifoStats{};
    const CdInstruments cd = cd_instruments();
    const app::IFrameSource::Counts shot = (frame_src_) ? frame_src_->counts()
                                                        : app::IFrameSource::Counts{};

    const app::VideoPumpStats vs = video_stats_.sample().value;
    const app::VideoGeometryRecord gr = video_geo_.sample().value;
    const app::VideoWire::Stats ws = video_wire_.stats();
    const app::VideoWire::GeoStats gs = video_wire_.geo_stats();
    const app::PreludeStats ps = prelude_stats_.sample().value;

    constexpr unsigned kRfSolve = 1u << 0;
    constexpr unsigned kRfZeroDivider = 1u << 1;
    constexpr unsigned kRfStage = 1u << 2;
    constexpr unsigned kRfStale = 1u << 3;
    constexpr unsigned kRfPartial = 1u << 4;
    constexpr unsigned kRfRejected = 1u << 5;
    constexpr unsigned kRfParseFallback = 1u << 6;
    constexpr unsigned kRfEmptySpec = 1u << 7;
    constexpr unsigned kRfFbCmdsDropped = 1u << 8;
    const unsigned rf_bits = (vs.solve_failures != 0u ? kRfSolve : 0u)
        | (vs.zero_divider != 0u ? kRfZeroDivider : 0u)
        | (vs.stage_failures != 0u ? kRfStage : 0u)
        | (ws.stale != 0u ? kRfStale : 0u)
        | (ws.partial != 0u ? kRfPartial : 0u)
        | (ws.rejected != 0u ? kRfRejected : 0u)
        | (vs.parse_fallbacks != 0u ? kRfParseFallback : 0u)
        | (vs.empty_specs != 0u ? kRfEmptySpec : 0u)
        | (vs.fb_cmds_dropped != 0u ? kRfFbCmdsDropped : 0u);

    const unsigned long long refused_wide = static_cast<unsigned long long>(vs.solve_failures)
        + vs.zero_divider
        + vs.stage_failures
        + static_cast<unsigned long long>(ws.stale)
        + ws.partial
        + ws.rejected
        + vs.empty_specs;
    const unsigned video_refused = app::sat_u32(refused_wide);

    static_assert(
        DiagSampler::kRtstatsWorstCase <= DiagSampler::kDiagLineCeiling,
        "the rtstats record's worst case must fit the xthread::DiagLog "
        "ceiling — raise kMaxLine in infra/diag_log.h or narrow a "
        "field, never let the sink swap the record for a "
        "{\"t\":\"trunc\"} marker"
    );

    static_assert(
        std::string_view{kRtstatsReasonWorstCase}.size()
                >= std::string_view{name(RecordReason::Cmd)}.size()
            && std::string_view{kRtstatsReasonWorstCase}.size()
                >= std::string_view{name(RecordReason::Alarm)}.size()
            && std::string_view{kRtstatsReasonWorstCase}.size()
                >= std::string_view{name(RecordReason::Final)}.size(),
        "TASTY_RTSTATS_WORST_ARGS must budget every RecordReason name"
    );

    static_assert(reactor::round_bucket_edge_us(reactor::kRoundBuckets - 1) == 536871u);
    static_assert(infra::ordinal(reactor::RoundSegment::kCount) - 1 == 4u);
    static_assert(kMaxServices - 1 == 23u);
    static_assert(reactor::kRoundLifecycleAll.mask() == 7u);

    const bool have_input = input_.has_value();
    const app::InputBuild::Counts build_counts = (have_input && input_build_)
        ? input_build_->counts()
        : app::InputBuild::Counts{};
    const app::InputStats ips = app::collect_input_stats(
        have_input ? *input_ : app::InputSample{},
        build_counts
    );

    if (have_input && input_->gates) {
        if (const auto fresh = gates_reader_.take_if_changed(*input_->gates))
            gates_seen_ = *fresh;
    }
    const svc::InputGates g = gates_seen_;
    constexpr unsigned kGrabbed = 1u;
    constexpr unsigned kOsd = kGrabbed << 1;
    constexpr unsigned kLost = kOsd << 1;
    const unsigned gate_word = (g.grabbed ? kGrabbed : 0u)
        | (g.osd_visible ? kOsd : 0u)
        | (g.keys_lost ? kLost : 0u);
    static_assert(svc::kMaxPlayers == 6);
    const svc::InputServiceCounts& isc = ips.service;
    const auto [hash0, hash1, hash2, hash3, hash4, hash5] = svc::printed_hashes(isc.slot_hash);
    const app::MglPumpStats ms = (mgl_stats_) ? mgl_stats_->sample().value : app::MglPumpStats{};
    const std::uint32_t mgl_row0 = mgl_row0_.get();

    const app::UiPageRecord up = (ui_pages_) ? ui_pages_->sample().value : app::UiPageRecord{};

    const reactor::FrameRecord fr = (frames_) ? frames_->sample().value : reactor::FrameRecord{};

    const reactor::RoundTiming rnd = round_timing_.sample().value;

    diag_.appendf(
        TASTY_RTSTATS_FMT,
        diag_seq_,
        name(reason),
        static_cast<unsigned long long>(stats_.heartbeat.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(diag_stats_.drains.get()),
        diag_stats_.rss_bytes.sample().value.text().c_str(),
        diag_stats_.heartbeat_stalls.get(),
        diag_stats_.rss_over_ceiling.get(),
        stats_.drops(Ring::cmd).get(),
        stats_.drops(Ring::event).get(),
        stats_.drops(Ring::prefetch).get(),
        stats_.drops(Ring::log).get(),
        stats_.spin_timeouts.get(),
        stats_.spurious_causes.get(),
        static_cast<unsigned>(events_.depth()),
        ev_lost,
        ev_lost_edge,
        fs.reads,
        fs.lines,
        fs.unrouted,
        fs.unrecognised,
        static_cast<unsigned>(dc.ladder_live),
        dc.ladder_pc.text().c_str(),
        dc.ladder_rungs,
        dc.ladder_bytes.text().c_str(),
        dc.ladder_assets,
        static_cast<unsigned>(dc.ladder_bios_found),
        static_cast<unsigned>(dc.ladder_disc_mounted),
        static_cast<unsigned>(dc.ladder_save_mounted),
        vs.edges,
        ws.staged,
        ws.emitted,
        ws.arm_fail,
        video_refused,
        rf_bits,
        static_cast<unsigned>(vs.last_mode_index.v),
        vs.last_fpix_khz,
        ps.fb_ack.text().c_str(),
        static_cast<unsigned>(gr.fb_en),
        static_cast<unsigned>(gr.valid),
        static_cast<unsigned>(gr.res),
        gr.width,
        gr.height,
        gs.samples,
        gs.changes,
        gs.resends,
        gs.errors,
        gs.late,
        gs.generation,
        gs.torn,
        ps.arms,
        ps.completions,
        ps.chunks,
        ps.coeff_words,
        ps.parked,
        ps.barrier_timeouts,
        ps.errors,
        ps.flt_flags.text().c_str(),
        ps.hdmi_int.text().c_str(),
        ps.gamma_cap.text().c_str(),
        ps.af_flags.text().c_str(),
        ps.afilter_skips,
        ps.tail_fail.text().c_str(),
        static_cast<unsigned>(ps.step),
        app::video_i2c_outcome_name(vs.i2c),
        static_cast<unsigned>(vs.i2c_bus),
        vs.i2c_writes.v,
        vs.i2c_errors.v,
        static_cast<unsigned>(app::reg_for_print(vs.i2c_fail_reg).v),
        static_cast<unsigned>(vs.i2c_first_op),
        static_cast<unsigned>(app::reg_for_print(vs.i2c_first_reg).v),
        vs.i2c_first_errno.v,
        vs.i2c_last_errno.v,
        vs.i2c_verify_errors,
        vs.i2c_poll_errors,
        vs.i2c_reprobes,
        vs.i2c_trips.v,
        static_cast<unsigned>(vs.i2c_breaker_open),
        vs.spd_sends,
        vs.spd_disables,
        vs.spd_dv1_sends,
        vs.spd_noname,
        static_cast<unsigned>(app::reg_for_print(vs.spd_fail_reg).v),
        ips.rounds,
        ips.events,
        ips.keys_emitted,
        ips.keys.keys_dropped.text().c_str(),
        ips.keys.key_sweeps.text().c_str(),
        ips.keys.keys_released.text().c_str(),
        ips.mouse_packets,
        ips.kicks,
        build_counts.rebuilds_done.text().c_str(),
        build_counts.rebuilds_refused.text().c_str(),
        ips.fd_evictions,
        ips.enumerate_failures,
        ips.rt_errors,
        ips.devices,
        ips.registered,
        isc.mapped.v,
        isc.slotted.v,
        gate_word,
        isc.mask_recomputes.v,
        ips.joy_seen.v,
        ips.joy_players.v,
        isc.axis_edges.v,
        isc.mouse_remainder_bytes.v,
        isc.key_overflows.v,
        ips.keys.menu_keys.text().c_str(),
        ips.keys.menu_keys_dropped.text().c_str(),
        isc.quirk_drops.v,
        ips.rejected,
        0u,
        up.button_actions.text().c_str(),
        isc.live.text().c_str(),
        isc.ghost_slots.v,
        hash0.text().c_str(),
        hash1.text().c_str(),
        hash2.text().c_str(),
        hash3.text().c_str(),
        hash4.text().c_str(),
        hash5.text().c_str(),
        ms.file_items,
        ms.abandoned,
        ms.armed,
        ms.publishes,
        mgl_row0,
        dc.file_tx_count,
        dc.file_tx_bytes.text().c_str(),
        static_cast<unsigned>(dc.file_tx_index.fio_byte()),
        dc.file_tx_window,
        dc.file_tx_window_refusals,
        static_cast<unsigned>(dc.file_tx_window_refusal_code),

        dc.blk_rounds,
        dc.blk_served,
        dc.blk_errors,
        static_cast<unsigned>(dc.blk_err_code),
        dc.blk_oversize,
        dc.blk_unencodable,
        dc.blk_blank_filled,
        dc.blk_write_failures,
        dc.blk_stock,
        dc.blk_config,
        dc.blk_discarded,
        dc.blk_deferred,
        dc.blk_expired,
        dc.blk_blocks,
        dc.blk_pf_expired,
        dc.blk_staging_expired,
        dc.blk_stale,

        db.declared,
        db.bound,
        db.refusals,
        db.fallbacks,
        db.retirements,
        db.release_failures,
        static_cast<unsigned>(ev_.online_cpus < 0 ? 0 : ev_.online_cpus),
        static_cast<unsigned>(ev_.cpu_coverage.applied),
        static_cast<unsigned long long>(rt_evidence_mask()),
        rt_name_mask(),
        rt_first_errno(),

        rnd.epoch,
        epoch_ms_of(rnd).count(),
        rnd.steady.n,
        reactor::round_us(rnd.steady.min_ns),
        reactor::round_us(rnd.steady.max_ns),
        reactor::round_p99_us(rnd.steady),
        rnd.steady.over_1ms,
        static_cast<unsigned>(rnd.steady.max_blame.seg),
        static_cast<unsigned>(rnd.steady.max_blame.index.wire()),
        rnd.lifecycle.n,
        reactor::round_us(rnd.lifecycle.max_ns),
        reactor::round_p99_us(rnd.lifecycle),
        static_cast<unsigned>(rnd.lifecycle.max_blame.seg),
        static_cast<unsigned>(rnd.lifecycle.max_blame.index.wire()),
        static_cast<unsigned>(rnd.lifecycle.max_blame.lifecycle_marks.mask()),
        rnd.wake.n,
        reactor::round_avg_us(rnd.wake),
        reactor::round_us(rnd.wake.max_ns),
        reactor::round_p99_us(rnd.wake),
        rnd.tick_overruns,
        rnd.all_steady_rounds,
        reactor::round_us(rnd.all_steady_max_ns),
        static_cast<unsigned>(rnd.all_steady_blame.seg),
        static_cast<unsigned>(rnd.all_steady_blame.index.wire()),
        rnd.steady_max_epoch,
        rnd.all_steady_over_1ms,
        rnd.all_life_rounds,
        reactor::round_us(rnd.all_life_max_ns),
        static_cast<unsigned>(rnd.all_life_blame.seg),
        static_cast<unsigned>(rnd.all_life_blame.index.wire()),
        static_cast<unsigned>(rnd.all_life_blame.lifecycle_marks.mask()),
        reactor::round_us(rnd.all_wake_max_ns),
        cd.deadline_misses,
        cd.deadline_slots,
        cd.prefetch_state,
        cd.hits,
        cd.misses,
        cd.decodes,
        cd.seeks,
        cd.drops,
        cd.errors,
        dc.disc_sync_decompress,
        dc.disc_park_timeouts,
        dc.disc_prefetch_refusals,

        dc.cd_flow_waits,
        dc.cd_cdda_sectors,

        dc.cd_data_sectors,
        dc.cd_idle_ticks,
        dc.cd_gated_ticks,
        static_cast<unsigned>(dc.cd_drive_state),
        dc.cd_drive_track,
        static_cast<int>(dc.cd_drive_lba),
        static_cast<unsigned>(dc.cd_drive_is_data),
        static_cast<int>(dc.cd_drive_audio_lba),
        dc.cd_substitutes,
        dc.cd_subcode_substitutes,
        dc.cd_not_resident,
        cd.decode_us,
        cd.evictions,
        dc.cd_busy_ticks,
        dc.cd_egress_abandons,

        up.depth,
        up.top_id,
        up.vis_publishes,
        up.cell_refusals,
        up.page_refusals.text().c_str(),
        up.button_levels,
        up.button_edges,
        up.button_longs,
        up.button_classic,
        up.button_injects,
        up.button_core_resets,
        up.last_hold_ms,
        fr.seq.v,
        shots_.drops(),
        shots_.result_drops(),
        shots_written_,
        shots_failed_,

        shot.captures,
        shot.refused,
        shot.torn,
        diag_.drops(),
        diag_.truncations()
    );
}

}
