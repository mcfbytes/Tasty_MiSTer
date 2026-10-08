// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/frame_copier.h"

#include <algorithm>
#include <chrono>
#include <optional>
#include <utility>

#include "app/cscd_codec.h"

namespace mister::app {

namespace {

using hal::ScalerBuffers;
using hal::ScalerHeader;

constexpr std::int64_t kMsNs = 1'000'000;

constexpr std::int64_t kLowlatGuardNs = 2 * kMsNs;

constexpr std::size_t kMinSlotBytes = 256u * 1024u;

constexpr std::uint8_t ctr_minus(std::uint8_t c, std::uint32_t n) noexcept {
    return static_cast<std::uint8_t>((c - n) & 0x7u);
}
constexpr std::uint8_t ctr_plus(std::uint8_t c, std::uint32_t n) noexcept {
    return static_cast<std::uint8_t>((c + n) & 0x7u);
}

constexpr std::array<std::uint16_t, 8> key_of(const ScalerHeader& h) noexcept {
    return {static_cast<std::uint16_t>(h.type << 8 | h.format),
            h.header_size,
            h.attributes,
            h.width,
            h.height,
            h.line,
            h.output_width,
            h.output_height};
}
constexpr ScalerHeader header_of(const std::array<std::uint16_t, 8>& k) noexcept {
    return {.type = static_cast<std::uint8_t>(k[0] >> 8),
            .format = static_cast<std::uint8_t>(k[0] & 0xFFu),
            .header_size = k[1],
            .attributes = k[2],
            .width = k[3],
            .height = k[4],
            .line = k[5],
            .output_width = k[6],
            .output_height = k[7]};
}

}  // namespace

const char* dup_reason_name(DupReason r) noexcept {
    switch (r) {
        case DupReason::None:
            return "none";
        case DupReason::Missed:
            return "missed";
        case DupReason::Torn:
            return "torn";
        case DupReason::Backpressure:
            return "backpressure";
        case DupReason::Resize:
            return "resize";
        case DupReason::Woven:
            return "woven";
        case DupReason::kCount:
            break;
    }
    return "?";
}

void FrameCopier::serve() noexcept {
    TASTY_SEAT_BODY(FrameCopier);
    reap_();
    sample_control_();
    std::int64_t now = w_.clock != nullptr ? w_.clock->now().count() : 0;
    switch (st_.state) {
        case RecState::Idle:
            break;
        case RecState::Probing:
            probe_step_(now);
            break;
        case RecState::Armed:
        case RecState::Recording:
            watch_step_();
            if (st_.state == RecState::Recording && !open_sent_ && !open_segment_()) break;
            if (st_.state == RecState::Recording && regrow_bytes_ != 0 && !regrow_()) break;
            if (st_.state == RecState::Armed || st_.state == RecState::Recording) {
                if (lowlat_) {
                    now = approach_(now);
                    poll_lowlat_(now);
                } else {
                    poll_triple_(now);
                }
            }
            break;
        case RecState::Closing:
            close_step_();
            break;
    }
    if (dirty_ && w_.status != nullptr) w_.status->publish(st_);
    dirty_ = false;
}

bool FrameCopier::idle() const noexcept {
    const bool control_moved = w_.control != nullptr && w_.control->generation() != 0 &&
                               w_.control->generation() != control_seen_.seen();
    return !control_moved;
}

int FrameCopier::park_ms() const noexcept {
    if (st_.state == RecState::Idle) return -1;
    const bool live = st_.state == RecState::Armed || st_.state == RecState::Recording;
    if (live && lowlat_ && ll_waiting_ && w_.clock != nullptr) {
        const std::int64_t left = ll_due_ns_ - w_.clock->now().count();

        if (left <= 0) return 0;
        if (left < kMsNs) return w_.delay != nullptr ? 0 : 1;
        return static_cast<int>(std::min<std::int64_t>(left / kMsNs, kPollMs));
    }
    return kPollMs;
}

void FrameCopier::release() noexcept {
    TASTY_SEAT_BODY(FrameCopier);
    reap_();
    if (st_.state != RecState::Idle && st_.state != RecState::Closing) end_(RecVerdict::Stopped);
    if (st_.state == RecState::Closing) close_step_();
    set_demand_(false);
    if (w_.status != nullptr) w_.status->publish(st_);
}

void FrameCopier::sample_control_() noexcept {
    RecControl c{};
    if (w_.control == nullptr || !control_seen_.take_if_changed(*w_.control, c)) return;
    switch (c.op) {
        case RecOp::Start:
        case RecOp::Arm:
            if (st_.state != RecState::Idle) return answer_(c.gen, RecVerdict::Busy);
            return begin_(c);
        case RecOp::Stop:
            answer_(c.gen, RecVerdict::Stopped);
            if (st_.state != RecState::Idle && st_.state != RecState::Closing)
                end_(RecVerdict::Stopped);
            return;
        case RecOp::Disarm: {
            const bool live = st_.state != RecState::Idle && st_.state != RecState::Closing;
            if (live && !from_arm_) return answer_(c.gen, RecVerdict::Busy);
            answer_(c.gen, RecVerdict::Stopped);
            if (live) end_(RecVerdict::Stopped);
            return;
        }
        case RecOp::None:
            return;
    }
}

void FrameCopier::answer_(std::uint16_t gen, RecVerdict v) noexcept {
    st_.answered = gen;
    st_.verdict = v;
    dirty_ = true;
}

void FrameCopier::begin_(const RecControl& c) noexcept {
    if (!window_ || w_.channel == nullptr || w_.clock == nullptr)
        return answer_(c.gen, RecVerdict::NoWindow);
    if (c.mode == RecMode::Avi && c.opt.codec != RecCodec::Zmbv && !CscdCodec::kAvailable)
        return answer_(c.gen, RecVerdict::NoCodec);
    const std::uint16_t answered = st_.answered;
    st_ = RecorderStatus{};
    st_.answered = answered;
    st_.gen = c.gen;
    st_.state = RecState::Probing;
    answer_(c.gen, RecVerdict::None);
    path_ = c.path;
    mode_ = c.mode;
    opt_ = c.opt;
    st_.avi = c.mode == RecMode::Avi ? 1 : 0;
    from_arm_ = c.op == RecOp::Arm;
    open_sent_ = close_sent_ = false;
    stride_ = 0;
    lowlat_ = false;
    regrow_bytes_ = 0;
    last_.reset();
    ext_ = 0;
    anchor_off_ = -1;
    cell_off_ = 0;
    cell_epoch_ = 0;
    core_seq_known_ = false;
    npending_ = 0;
    have_cell_ = false;
    lag_steps_ = 1;
    woven_ = false;
    matched_ = true;
    ll_waiting_ = false;
    ll_push_ns_ = 0;
    replay_running_ = tail_counting_ = false;
    tail_left_ = 0;
    nvotes_ = 0;
    covers_zero_ = false;
    epoch_wait_ns_ = -1;
    ReplayStatus rs{};
    if (from_arm_ && w_.replay != nullptr && w_.replay->sample_into(rs) != 0) note_replay_(rs);
    period_ns_ = kDefaultPeriodNs;
    const std::int64_t t0 = w_.clock->now().count();
    probe_.restart(t0);
    armed_ns_ = t0;
    set_demand_(true);
    probe_.read(*window_);
}

void FrameCopier::end_(RecVerdict why) noexcept {
    st_.end = why;
    st_.state = RecState::Closing;
    ll_waiting_ = false;
    dirty_ = true;
    close_step_();
}

void FrameCopier::set_demand_(bool on) noexcept {
    if (on == demand_on_) return;
    demand_on_ = on;
    if (w_.demand != nullptr) w_.demand->publish(FrameDemand{.on = static_cast<std::uint8_t>(on)});
}

void FrameCopier::reap_() noexcept {
    if (w_.channel == nullptr) return;
    for (std::size_t i = 0; i < kRawFrameSlots; ++i) {
        auto back = w_.channel->reap();
        if (!back) break;
    }
}

bool FrameCopier::all_home_() const noexcept {
    const auto c = w_.channel->census();
    return c.idle == c.live;
}

void FrameCopier::probe_step_(std::int64_t now) noexcept {

    if (ReplayStatus rs{}; from_arm_ && w_.replay != nullptr && w_.replay->sample_into(rs) != 0)
        note_replay_(rs);
    probe_.read(*window_);
    const auto d = probe_.decide(now);
    if (d == hal::ScalerProbe::Verdict::Pending) return;
    if (d == hal::ScalerProbe::Verdict::Dead) {
        if (from_arm_ && now - armed_ns_ < kEpochWaitNs && replay_before_epoch_()) {
            probe_.restart(now);
            return;
        }
        const RecVerdict v = hal::ScalerProbe::port_stuck(*window_) ? RecVerdict::ScalerPortStuck
                                                                    : RecVerdict::NoLiveBuffer;
        answer_(st_.gen, v);
        end_(v);
        return;
    }
    lowlat_ = d == hal::ScalerProbe::Verdict::LargeLowlat;
    stride_ = d == hal::ScalerProbe::Verdict::Small ? ScalerBuffers::kStrideSmall
                                                    : ScalerBuffers::kStrideLarge;
    st_.stride_mib = static_cast<std::uint8_t>(stride_ >> 20);
    st_.lowlat = lowlat_ ? 1 : 0;
    dirty_ = true;

    track_reset_(probe_.moved(stride_), now);

    std::size_t bytes = 0;
    for (std::size_t i = 0; i < ScalerBuffers::kBuffers; ++i) {
        const auto h = window_->header(base_(i));
        if (live_[i] && h && h->supported())
            bytes = std::max(bytes, static_cast<std::size_t>(h->line) * h->height);
    }
    if (!reserve_arena_(std::max(bytes, kMinSlotBytes))) {
        answer_(st_.gen, RecVerdict::NoMemory);
        return end_(RecVerdict::NoMemory);
    }
    answer_(st_.gen, RecVerdict::Started);
    st_.state = from_arm_ ? RecState::Armed : RecState::Recording;
    if (!from_arm_) (void)open_segment_();
}

bool FrameCopier::open_segment_() noexcept {
    auto loan = w_.channel->acquire();
    if (!loan) return false;
    *loan = RawFrameSlot{};
    loan->kind = RawKind::Open;
    loan->gen = st_.gen;
    loan->mode = mode_;
    loan->opt = opt_;
    loan->path = path_;
    w_.channel->send(std::move(loan));
    open_sent_ = true;
    core_seq_known_ = false;
    st_.state = RecState::Recording;
    dirty_ = true;
    return true;
}

void FrameCopier::close_step_() noexcept {
    if (open_sent_ && !close_sent_) {
        auto loan = w_.channel->acquire();
        if (!loan) return;
        *loan = RawFrameSlot{};
        loan->kind = RawKind::Close;
        loan->gen = st_.gen;
        attach_pending_(*loan);
        w_.channel->send(std::move(loan));
        close_sent_ = true;
    }
    if (w_.channel != nullptr && !all_home_()) return;

    arena_.release();
    st_.arena_kib = 0;
    st_.state = RecState::Idle;
    set_demand_(false);
    dirty_ = true;
}

void FrameCopier::attach_pending_(RawFrameSlot& slot) noexcept {
    if (npending_ == 0) return;
    slot.runs = npending_;
    std::uint64_t end = 0;
    for (std::uint8_t i = 0; i < npending_; ++i) {
        slot.run[i] = pending_[i];
        end = pending_[i].first + pending_[i].count;
        st_.rows += pending_[i].count;
    }
    npending_ = 0;
    slot.width = st_.width;
    slot.height = st_.height;
    slot.stamp.core_frame = end;
    if (have_cell_) slot.stamp.movie_frame = movie_of_(last_cell_, end);
    dirty_ = true;
}

bool FrameCopier::regrow_() noexcept {
    if (!all_home_()) return true;
    const std::size_t want = regrow_bytes_;
    regrow_bytes_ = 0;
    if (!reserve_arena_(want)) {
        end_(RecVerdict::NoMemory);
        return false;
    }
    return true;
}

bool FrameCopier::reserve_arena_(std::size_t slot_bytes) noexcept {
    const std::size_t want = raw_depth(slot_bytes);
    const std::size_t depth =
        w_.channel->set_live(static_cast<std::uint8_t>(want)) ? want : w_.channel->live();
    if (auto r = arena_.reserve(depth, slot_bytes); !r) return false;
    st_.arena_kib = static_cast<std::uint32_t>(arena_.bytes() / 1024u);
    st_.depth = static_cast<std::uint8_t>(depth);
    dirty_ = true;
    return true;
}

void FrameCopier::watch_step_() noexcept {
    if (st_.state == RecState::Recording && w_.writer != nullptr) {
        RecWriteStatus ws{};
        if (w_.writer->sample_into(ws) != 0 && ws.gen == st_.gen &&
            ws.state == RecWriteState::Failed)
            return end_(RecVerdict::WriterFailed);
    }
    if (st_.state == RecState::Recording && w_.avi_writer != nullptr) {
        AviWriteStatus as{};
        if (w_.avi_writer->sample_into(as) != 0 && as.gen == st_.gen &&
            as.state == RecWriteState::Failed)
            return end_(RecVerdict::WriterFailed);
    }
    if (st_.state == RecState::Recording && w_.frames != nullptr) {
        CoreFrameRecord f{};
        if (w_.frames->sample_into(f) != 0) {
            if (!core_seq_known_) {
                core_seq_ = f.core_seq;
                core_seq_known_ = true;
            } else if (f.core_seq != core_seq_) {
                return end_(RecVerdict::CoreSwitch);
            }
        }
    }
    if (from_arm_ && w_.replay != nullptr) {
        ReplayStatus r{};
        if (w_.replay->sample_into(r) == 0) return;
        if (st_.state == RecState::Armed) note_replay_(r);
        if (r.level == ReplayLevel::Running) {
            replay_running_ = true;
            epoch_wait_ns_ = -1;
        } else if (r.end == ReplayEnd::EpochAmbiguous || r.level == ReplayLevel::AwaitPowerOn) {
            replay_running_ = false;

            const std::int64_t now = w_.clock->now().count();
            if (epoch_wait_ns_ < 0) epoch_wait_ns_ = now;
            if (now - epoch_wait_ns_ > kEpochWaitNs) return end_(RecVerdict::ReplayEnded);
        } else if (replay_running_ && st_.state == RecState::Armed) {
            return end_(RecVerdict::ReplayEnded);
        } else if (replay_running_ && !tail_counting_) {
            tail_counting_ = true;
            tail_left_ = kReplayTailFrames;
        }
    }
}

void FrameCopier::note_replay_(const ReplayStatus& r) noexcept {

    if (r.level != ReplayLevel::Running || r.movie_frame < 0) covers_zero_ = true;
}

bool FrameCopier::replay_before_epoch_() const noexcept {
    if (w_.replay == nullptr) return false;
    ReplayStatus r{};
    if (w_.replay->sample_into(r) == 0) return false;
    if (r.level == ReplayLevel::AwaitPowerOn) return true;
    if (r.level == ReplayLevel::Idle &&
        (r.end == ReplayEnd::None || r.end == ReplayEnd::EpochAmbiguous))
        return true;
    return false;
}

FrameCopier::Heads FrameCopier::read_heads_() const noexcept {
    Heads out;
    for (std::size_t i = 0; i < ScalerBuffers::kBuffers; ++i) {
        const auto h = window_->header(base_(i));
        if (!h) return out;
        out.h[i] = *h;
    }
    out.ok = true;
    return out;
}

void FrameCopier::poll_triple_(std::int64_t now) noexcept {
    const Heads hd = read_heads_();
    if (!hd.ok) {
        ++st_.bad_header;
        dirty_ = true;
        return;
    }
    track_(hd, now);
    std::size_t nlive = 0;
    for (std::size_t i = 0; i < ScalerBuffers::kBuffers; ++i) {
        if (!live_[i]) continue;
        ++nlive;
        if (hd.h[i].supported()) continue;
        ++st_.bad_header;
        dirty_ = true;
        return;
    }

    if (live_[0] && !hd.h[0].triple_buffered()) return follow_mode_(true, now);
    const auto pick = hal::ScalerProbe::pick_complete(hd.h, live_);
    if (!pick) {

        if (nlive < 2) return;
        if (matched_) {
            ++st_.unmatched;
            dirty_ = true;
        }
        matched_ = false;
        return;
    }
    matched_ = true;
    const std::uint8_t ctr = hd.h[pick->buf].frame_counter();
    if (last_ && ctr == last_->ctr) return;
    lag_steps_ = pick->lag;

    if (!pick->woven && pick->prev != ScalerBuffers::kBuffers && last_ &&
        ctr_minus(ctr, last_->ctr) >= 2) {
        on_complete_(pick->prev, ctr_minus(ctr, 1), now, 1, 1);

        if (st_.state != RecState::Armed && st_.state != RecState::Recording) return;
        if (regrow_bytes_ != 0) return;
    }

    on_complete_(pick->buf, ctr, now, 0, pick->woven ? 2u : 1u);
    if (woven_ != pick->woven) {
        woven_ = pick->woven;
        st_.interlaced = woven_ ? 1 : 0;
        dirty_ = true;
    }
}

void FrameCopier::track_(const Heads& hd, std::int64_t now) noexcept {

    if (now - track_ns_ > period_ns_)
        for (std::size_t i = 0; i < ScalerBuffers::kBuffers; ++i)
            seen_ns_[i] = now;
    track_ns_ = now;
    bool odd = false;
    bool parked = false;
    for (std::size_t i = 0; i < ScalerBuffers::kBuffers; ++i) {
        const ScalerHeader& h = hd.h[i];
        const auto key = key_of(h);
        if (key != seen_key_[i]) {

            const ScalerHeader was = header_of(seen_key_[i]);

            odd = odd || (live_[i] && h.supported() && was.supported() && !h.interlaced() &&
                          (ctr_minus(h.frame_counter(), was.frame_counter()) & 1u) != 0);
            seen_key_[i] = key;
            seen_ns_[i] = now;

            live_[i] = live_[i] || h.supported();
        } else if (now - seen_ns_[i] > kParkPeriods * period_ns_) {
            live_[i] = false;
        }
        parked = parked || !live_[i];
    }

    if (!odd || !parked) return;
    for (std::size_t i = 0; i < ScalerBuffers::kBuffers; ++i) {
        if (live_[i] || !hd.h[i].supported()) continue;
        live_[i] = true;
        seen_ns_[i] = now;
    }
}

void FrameCopier::track_reset_(const BufMask& live, std::int64_t now) noexcept {
    for (std::size_t i = 0; i < ScalerBuffers::kBuffers; ++i) {
        const auto h = window_->header(base_(i));
        seen_key_[i] = key_of(h ? *h : ScalerHeader{});
        seen_ns_[i] = now;
        live_[i] = live[i];
    }
    track_ns_ = now;
}

void FrameCopier::poll_lowlat_(std::int64_t now) noexcept {
    const auto h = window_->header(0);
    if (!h || !h->supported()) {
        ++st_.bad_header;
        dirty_ = true;
        return;
    }
    if (h->triple_buffered()) return follow_mode_(false, now);
    const std::uint8_t c = h->frame_counter();
    const bool fresh = !last_ || c != last_->ctr;
    if (fresh && (!ll_waiting_ || c != ll_ctr_)) {

        std::int64_t push = now;
        if (ll_push_ns_ != 0) {
            const std::int64_t k = (now - ll_push_ns_ + period_ns_ / 2) / period_ns_;
            const std::int64_t predicted = ll_push_ns_ + k * period_ns_;
            if (predicted <= now && now - predicted < kPollMs * kMsNs) push = predicted;
        }
        ll_push_ns_ = push;
        ll_ctr_ = c;
        ll_waiting_ = true;
        ll_due_ns_ = push + period_ns_ - copy_ns_ - kLowlatGuardNs;
    }
    if (ll_waiting_ && now >= ll_due_ns_) {
        ll_waiting_ = false;
        on_complete_(0, ll_ctr_, now, 0, 1);
    }
}

void FrameCopier::follow_mode_(bool single, std::int64_t now) noexcept {

    if (!single) track_reset_(BufMask{true, false, false}, now);
    lowlat_ = single;
    st_.lowlat = single ? 1 : 0;
    ll_waiting_ = false;
    ll_push_ns_ = 0;
    dirty_ = true;
}

std::int64_t FrameCopier::approach_(std::int64_t now) noexcept {
    if (!ll_waiting_ || w_.delay == nullptr || now >= ll_due_ns_ || ll_due_ns_ - now >= kMsNs)
        return now;
    w_.delay->sleep_until(std::chrono::nanoseconds{ll_due_ns_});
    return w_.clock->now().count();
}

std::int32_t FrameCopier::movie_of_(const CoreFrameRecord& cell,
                                    std::uint64_t core) const noexcept {
    if (cell.movie_frame == -1) return -1;
    const std::int64_t m =
        static_cast<std::int64_t>(cell.movie_frame) +
        (static_cast<std::int64_t>(core) + cell_off_ - static_cast<std::int64_t>(cell.core_frame));
    return static_cast<std::int32_t>(std::max<std::int64_t>(m, -0x7FFF'FFFF));
}

void FrameCopier::anchor_(const CoreFrameRecord& cell, std::int64_t lag, bool vote) noexcept {

    const std::int64_t off =
        static_cast<std::int64_t>(cell.core_frame) - lag - static_cast<std::int64_t>(ext_);

    if (st_.anchored == 0 || cell.epoch != cell_epoch_ || off > anchor_off_ + 1 ||
        off < anchor_off_ - 1)
        nvotes_ = vote_next_ = 0;
    if (!vote && nvotes_ != 0) return;
    votes_[vote_next_] = off;
    vote_next_ = static_cast<std::uint8_t>((vote_next_ + 1u) % kAnchorVotes);
    if (nvotes_ < kAnchorVotes) ++nvotes_;
    const auto first = votes_.begin();
    const auto last = first + nvotes_;
    std::ptrdiff_t best = 0;
    for (auto v = first; v != last; ++v) {
        const std::ptrdiff_t n = std::count(first, last, *v);
        if (n > best || (n == best && *v > anchor_off_)) {
            best = n;
            anchor_off_ = *v;
        }
    }
    cell_off_ = 0;
    cell_epoch_ = cell.epoch;
    st_.anchored = 1;
    dirty_ = true;
}

void FrameCopier::rebase_(const CoreFrameRecord& cell, std::uint64_t core,
                          std::int64_t lag) noexcept {

    cell_off_ = static_cast<std::int64_t>(cell.core_frame) - lag - static_cast<std::int64_t>(core);
    cell_epoch_ = cell.epoch;
    ++st_.rebased;
    dirty_ = true;
}

void FrameCopier::cross_check_(const CoreFrameRecord& cell, std::uint64_t core,
                               std::int64_t lag) noexcept {
    const std::int64_t d = static_cast<std::int64_t>(cell.core_frame) - lag -
                           (static_cast<std::int64_t>(core) + cell_off_);
    if (d > 1 || d < -1) {
        ++st_.stamp_drift;
        dirty_ = true;
    }
}

std::uint32_t FrameCopier::frames_since_(std::uint8_t ctr, std::int64_t now,
                                         const CoreFrameRecord* cell,
                                         std::int64_t behind) noexcept {
    if (!last_) {
        last_ = LastCtr{ctr, now};
        return 1;
    }
    const std::uint32_t d = ctr_minus(ctr, last_->ctr);
    const std::int64_t dt = now - last_->seen_ns;
    std::uint32_t n = d;
    const std::int64_t est = (dt + period_ns_ / 2) / period_ns_;
    if (est >= 7) {

        n = d + 8u * static_cast<std::uint32_t>((est - d + 4) / 8);
        if (st_.anchored != 0 && cell != nullptr && cell->epoch == cell_epoch_) {
            const std::int64_t last = static_cast<std::int64_t>(ext_) + anchor_off_ + cell_off_;
            const std::int64_t by_cell =
                static_cast<std::int64_t>(cell->core_frame) - lag_(behind) - last;

            if (by_cell + 1 >= est)
                n = d + 8u * static_cast<std::uint32_t>(
                                 std::max<std::int64_t>(0, (by_cell - std::int64_t{d} + 4) / 8));
        }
        ++st_.gap_estimated;
        nvotes_ = vote_next_ = 0;
    } else if (const std::uint32_t step = woven_ ? 2u : 1u;
               d == step && behind == 0 && dt > 0 && dt < std::int64_t{step + 1} * period_ns_) {

        period_ns_ = (7 * period_ns_ + dt / step) / 8;
    }
    last_ = LastCtr{ctr, now};
    return n;
}

FrameStamp FrameCopier::stamp_(std::uint8_t ctr, std::int64_t now, const CoreFrameRecord* cell,
                               std::int64_t lag) noexcept {
    FrameStamp s{};
    s.header_ctr = ctr;
    s.capture_ns = now;
    const std::uint64_t core =
        static_cast<std::uint64_t>(static_cast<std::int64_t>(ext_) + anchor_off_);
    s.core_frame = core;

    if (cell == nullptr && st_.anchored != 0 && have_cell_ && last_cell_.epoch == cell_epoch_)
        s.movie_frame = movie_of_(last_cell_, core);
    if (cell == nullptr || st_.anchored == 0) return s;
    if (cell->epoch != cell_epoch_) rebase_(*cell, core, lag);
    cross_check_(*cell, core, lag);
    s.movie_frame = movie_of_(*cell, core);
    return s;
}

bool FrameCopier::open_armed_(const FrameStamp& s, const CoreFrameRecord* cell,
                              std::uint32_t fields, std::int64_t now) noexcept {

    if (cell == nullptr || cell->movie_frame < 0) return false;
    if (s.movie_frame + static_cast<std::int32_t>(fields) - 1 < 0) return false;
    if (!open_segment_()) return false;
    std::int64_t owed = covers_zero_ ? s.movie_frame : 0;

    owed = std::min<std::int64_t>(owed, (now - armed_ns_) / std::max<std::int64_t>(period_ns_, 1));
    if (owed > 0) {
        const auto n = static_cast<std::uint32_t>(owed);
        st_.missed += n;
        st_.first_core_frame = s.core_frame - n;
        defer_(st_.first_core_frame, n, ctr_minus(s.header_ctr, n), DupReason::Missed);
    }
    return true;
}

void FrameCopier::on_complete_(std::size_t buf, std::uint8_t ctr, std::int64_t now,
                               std::int64_t behind, std::uint32_t fields) noexcept {

    CoreFrameRecord rec{};
    const bool have =
        w_.frames != nullptr && w_.frames->sample_into(rec) != 0 && rec.supported != 0;
    const CoreFrameRecord* cell = have ? &rec : nullptr;
    if (have) {
        last_cell_ = rec;
        have_cell_ = true;
    }
    const std::int64_t lag = lag_(behind);
    const std::uint32_t n = frames_since_(ctr, now, cell, behind);
    ext_ += n;

    if (have && (st_.anchored == 0 || st_.state == RecState::Armed) && st_.rows == 0 &&
        npending_ == 0)
        anchor_(rec, lag, behind == 0);
    FrameStamp s = stamp_(ctr, now, cell, lag);
    if (st_.state == RecState::Armed) {
        if (!open_armed_(s, cell, fields, now)) return;
    } else if (n > 1) {

        const std::uint32_t woven = woven_ ? 1u : 0u;
        const std::uint32_t missed = n - 1 - woven;
        if (woven != 0) {

            const std::uint64_t at = s.core_frame - (n - 1);
            const FrameStampRun* last = npending_ > 0 ? &pending_[npending_ - 1] : nullptr;
            const bool held = last != nullptr && last->first + last->count == at;
            if (!held) ++st_.woven;
            defer_(at, 1, ctr_minus(ctr, n - 1), held ? last->why : DupReason::Woven);
        }
        if (missed != 0) {
            st_.missed += missed;
            defer_(s.core_frame - missed, missed, ctr_minus(ctr, missed), DupReason::Missed);
        }
    }
    if (st_.rows == 0 && npending_ == 0) st_.first_core_frame = s.core_frame;
    const auto h = window_->header(base_(buf));
    if (!h || !geometry_ok_(*h)) {
        ++st_.bad_header;
        dirty_ = true;
        return defer_(s.core_frame, 1, ctr, DupReason::Missed);
    }
    deliver_(buf, *h, s);
}

bool FrameCopier::geometry_ok_(const ScalerHeader& h) const noexcept {
    const std::size_t line = h.line;
    if (!h.supported() || h.width == 0 || h.height == 0) return false;
    if (line < static_cast<std::size_t>(h.width) * 3u || h.header_size < ScalerHeader::kBytes)
        return false;
    return h.header_size + line * h.height <= stride_;
}

void FrameCopier::defer_(std::uint64_t first, std::uint32_t count, std::uint8_t first_ctr,
                         DupReason why) noexcept {
    if (count == 0) return;
    if (npending_ > 0) {
        FrameStampRun& last = pending_[npending_ - 1];
        const bool adjacent = last.first + last.count == first;

        if ((adjacent && last.why == why) || npending_ == RawFrameSlot::kMaxRuns) {
            last.count += count;
            return;
        }
    }
    pending_[npending_++] =
        FrameStampRun{.first = first, .count = count, .first_ctr = first_ctr, .why = why};
}

bool FrameCopier::rate_is_ours_(std::uint32_t seq) const noexcept {
    if (core_seq_known_) return seq == core_seq_;
    if (w_.frames == nullptr || w_.frames->generation() == 0) return true;
    CoreFrameRecord f{};
    return w_.frames->sample_into(f) != 0 && seq == f.core_seq;
}

void FrameCopier::deliver_(std::size_t buf, const ScalerHeader& h,
                           const FrameStamp& stamp) noexcept {
    auto loan = w_.channel->acquire();
    if (!loan) {
        ++st_.backpressure;
        dirty_ = true;
        return defer_(stamp.core_frame, 1, stamp.header_ctr, DupReason::Backpressure);
    }
    FrameStamp s = stamp;
    RawFrameSlot& slot = *loan;
    slot = RawFrameSlot{};
    slot.kind = RawKind::Frame;
    slot.gen = st_.gen;
    slot.runs = npending_;
    slot.depth = st_.depth;
    std::uint32_t rows = 1;
    for (std::uint8_t i = 0; i < npending_; ++i) {
        slot.run[i] = pending_[i];
        rows += pending_[i].count;
    }
    npending_ = 0;
    slot.width = h.width;
    slot.height = h.height;
    slot.line = h.line;
    VideoGeometryRecord g{};
    if (w_.video != nullptr && w_.video->sample_into(g) != 0 && g.valid &&
        rate_is_ours_(g.core_seq))
        slot.vtime = g.vtime;
    const std::size_t bytes = static_cast<std::size_t>(h.line) * h.height;
    const std::span<std::byte> stripe = arena_.stripe(w_.channel->index_of(loan));
    if (bytes > stripe.size()) {
        s.dup = DupReason::Resize;
        ++st_.resize;
        regrow_bytes_ = std::max(regrow_bytes_, bytes);
    } else {
        const std::int64_t t0 = w_.clock->now().count();
        const auto r = window_->copy(base_(buf) + h.header_size, stripe.first(bytes));
        const std::int64_t t1 = w_.clock->now().count();
        const auto after = window_->header(base_(buf));
        copy_ns_ = (3 * copy_ns_ + (t1 - t0)) / 4;
        st_.copy_us_last = static_cast<std::uint32_t>((t1 - t0) / 1000);
        st_.copy_us_max = std::max(st_.copy_us_max, st_.copy_us_last);

        if (!r || !after || after->frame_counter() != s.header_ctr) {
            s.dup = DupReason::Torn;
            ++st_.torn;
        } else {
            ++st_.captured;
            slot.pixels = stripe.data();
        }
    }
    slot.stamp = s;
    w_.channel->send(std::move(loan));
    st_.rows += rows;
    st_.last_core_frame = s.core_frame;
    st_.width = h.width;
    st_.height = h.height;
    dirty_ = true;
    if (tail_counting_) {
        tail_left_ = rows >= tail_left_ ? 0 : tail_left_ - rows;
        if (tail_left_ == 0) end_(RecVerdict::ReplayEnded);
    }
}

}  // namespace mister::app
