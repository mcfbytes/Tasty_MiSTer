// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/avi_encoder.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string_view>
#include <utility>

#include "app/avi_format.h"

namespace mister::app {

namespace {

constexpr std::size_t kDescBytes = ChunkSlot::kMaxChunks * sizeof(std::uint32_t);
constexpr std::int64_t kMsNs = 1'000'000;

constexpr std::size_t kMinChunkDepth = 4;

}  // namespace

std::int64_t AviEncoder::now_() const noexcept {
    return w_.clock != nullptr ? w_.clock->now().count() : 0;
}

std::int64_t AviEncoder::cpu_now_() const noexcept {
    return w_.cpu != nullptr ? w_.cpu->now().count() : now_();
}

bool AviEncoder::take_dirty() noexcept { return std::exchange(dirty_, false); }

RecPath AviEncoder::segment_path(const RecPath& sidecar, std::uint16_t index) noexcept {
    std::string_view base = sidecar.view();
    for (const std::string_view suffix :
         {std::string_view{".frames.tsv"}, std::string_view{".tsv"}}) {
        if (base.size() > suffix.size() && base.ends_with(suffix)) {
            base.remove_suffix(suffix.size());
            break;
        }
    }
    char buf[RecPath::kBufSize];
    const int n = std::snprintf(buf, sizeof buf, "%.*s_%03u.avi", static_cast<int>(base.size()),
                                base.data(), static_cast<unsigned>(index));
    RecPath out{};
    if (n > 0 && static_cast<std::size_t>(n) < sizeof buf)
        (void)out.assign(std::string_view(buf, static_cast<std::size_t>(n)));
    return out;
}

bool AviEncoder::open(const RawFrameSlot& s) noexcept {
    TASTY_SEAT_BODY(AviEncoder);
    flush_();
    pos_ = Position{};
    active_ = w_.out != nullptr && s.mode == RecMode::Avi;
    if (!active_) return true;
    gen_ = s.gen;
    sidecar_ = s.path;
    opt_ = s.opt;
    codec_ = &codec_for_(opt_.codec);
    step_ = opt_.scale == RecScale::Auto;
    scale_ = opt_.scale == RecScale::Half ? 2 : 1;
    frame_mul_ = opt_.every == 0 ? 1 : opt_.every;
    seg_limit_ =
        opt_.segment_bytes != 0 ? std::min(opt_.segment_bytes, kRecSegmentMax) : lim_.segment_bytes;
    broken_ = false;
    need_roll_ = false;
    seg_open_ = false;
    seg_next_ = 0;
    seg_rate_ = 0;
    roll_rate_ = 0;
    cand_ = 0;
    cand_run_ = 0;
    need_key_ = true;
    since_key_ = 0;
    win_start_ = -1;
    win_cpu_ = 0;
    win_over_ = 0;
    cause_ = HalfCause::None;
    calm_ns_ = 0;
    returned_ns_ = -1;
    cscd_.end();
    zmbv_.end();
    st_ = EncodeStatus::Video{};
    st_.scale = scale_;

    const Reserve got = chunks_.bytes() == 0 ? reserve_chunks_(0) : Reserve::Ok;
    if (got == Reserve::Busy) return false;
    if (got == Reserve::NoMemory) {
        broken_ = true;
        data_cap_ = 0;
        dirty_ = true;
        if (!marker_(ChunkKind::Refuse, 0)) return false;
        ++st_.errors;
        return true;
    }
    st_.arena_kib = static_cast<std::uint32_t>(chunks_.bytes() / 1024u);
    codec_->restart(opt_);
    st_.me_rad = codec_->search().radius;
    dirty_ = true;
    return true;
}

bool AviEncoder::close(const RawFrameSlot& s) noexcept {
    TASTY_SEAT_BODY(AviEncoder);
    if (!active_ || s.gen != gen_) return true;
    flush_();
    if (!marker_(ChunkKind::Close, 0)) return false;
    active_ = false;
    seg_open_ = false;
    cscd_.end();
    zmbv_.end();
    half_.release();
    pos_ = Position{};
    dirty_ = true;
    return true;
}

bool AviEncoder::kept_(std::uint64_t core, std::int32_t movie) const noexcept {
    const RecOptions& o = opt_;
    if (o.from_frame >= 0 || o.to_frame >= 0) {
        if (movie < 0) return false;
        if (o.from_frame >= 0 && movie < o.from_frame) return false;
        if (o.to_frame >= 0 && movie >= o.to_frame) return false;
    }
    const std::uint32_t every = o.every == 0 ? 1u : o.every;
    if (every <= 1) return true;

    if (o.from_frame >= 0) return static_cast<std::uint32_t>(movie - o.from_frame) % every == 0;
    return core % every == 0;
}

IFrameCodec& AviEncoder::codec_for_(RecCodec c) noexcept {
    switch (c) {
        case RecCodec::Cscd:
            return cscd_;
        case RecCodec::Zmbv:
            return zmbv_;
    }
    return cscd_;
}

bool AviEncoder::chunk_one_(IFrameCodec& codec, const RawFrameSlot& s, bool real,
                            std::uint32_t cand, std::uint32_t run, bool stable) noexcept {
    const auto ow = static_cast<std::uint16_t>(scale_ == 2 ? s.width / 2u : s.width);
    const auto oh = static_cast<std::uint16_t>(scale_ == 2 ? s.height / 2u : s.height);
    if (ow == 0 || oh == 0) {
        ++st_.errors;
        dirty_ = true;
        return true;
    }
    if (!codec.open() || (real && (ow != codec.width() || oh != codec.height()))) {
        if (auto r = codec.begin(ow, oh); !r) {
            ++st_.errors;
            dirty_ = true;
            return true;
        }
        seg_open_ = false;
        seg_rate_ = 0;
        roll_rate_ = 0;
    }
    if (real && need_roll_) {
        seg_open_ = false;
        need_roll_ = false;
    }
    if (seg_open_ && seg_rate_ != 0 && stable && !same_rate(cand, seg_rate_)) {
        seg_open_ = false;
        roll_rate_ = cand;
        ++st_.rate_rolls;
    }
    const std::size_t pmax = codec.max_payload();
    if (seg_open_ && size_roll_(pmax)) seg_open_ = false;
    if (!seg_open_ && !open_segment_(roll_rate_ != 0 ? roll_rate_ : seg_rate_)) return false;
    roll_rate_ = 0;
    std::byte* const dst = reserve_(pmax);
    if (dst == nullptr) return broken_;
    const std::span<std::byte> out{dst, pmax};
    std::size_t n = 0;
    bool key = false;
    bool lost = false;
    const std::byte* src = nullptr;
    if (real) {
        key = need_key_ || since_key_ >= lim_.key_interval;
        const std::int64_t c0 = cpu_now_();
        src = s.pixels;
        std::size_t line = s.line;
        if (scale_ == 2) {
            const std::size_t bytes = std::size_t{ow} * 3u * oh;
            if (half_.slot_bytes() < bytes && !half_.reserve(1, bytes)) src = nullptr;
            if (src != nullptr) {
                CscdCodec::half_scale(s.pixels, s.line, s.width, s.height, half_.stripe(0).data());
                src = half_.stripe(0).data();
                line = std::size_t{ow} * 3u;
            }
        }
        n = src != nullptr ? codec.encode(src, line, key, out) : 0;
        const std::int64_t spent = cpu_now_() - c0;
        st_.enc_us_last = static_cast<std::uint32_t>(spent / 1000);
        st_.enc_us_max = std::max(st_.enc_us_max, st_.enc_us_last);
        if (n != 0) {
            ++st_.encoded;
            budget_(spent, now_());
        }
    }
    if (n == 0) {
        if (real) {
            ++st_.errors;
            need_key_ = true;
            lost = src == nullptr;
        }
        if (need_key_) {
            n = codec.rekey(out);
            key = true;
        } else {
            const auto d = codec.dup();
            if (d.empty() || d.size() > pmax) return true;
            std::memcpy(dst, d.data(), d.size());
            n = d.size();
            key = false;
        }
        if (n == 0) return true;
        ++st_.dups;
    }
    commit_(n, key);
    cand_ = cand;
    cand_run_ = run;
    if (seg_rate_ == 0 && stable) seg_rate_ = cand;
    if (seg_rate_ != 0) loan_->vtime = seg_rate_;
    pos_ = Position{
        .segment = st_.segment, .frame = static_cast<std::int32_t>(seg_frames_ - 1), .lost = lost};
    if (key) {
        need_key_ = false;
        since_key_ = 0;
        ++st_.keys;
    } else {
        ++since_key_;
    }

    const std::size_t queued = w_.raw != nullptr ? w_.raw->outbound() + 1 : 0;
    const std::size_t depth = s.depth != 0 ? s.depth : kRawFrameSlots;
    const bool full = w_.raw != nullptr && queued >= depth;
    if (real)
        st_.queue_max = std::max<std::uint8_t>(st_.queue_max, static_cast<std::uint8_t>(queued));
    if (step_ && real && !held_ && full && scale_ == 1) step_half_(true, now_());

    if (real && queued * 4 <= depth) held_ = false;
    if (real && w_.raw != nullptr) recover_(queued, depth, now_());
    dirty_ = true;
    return true;
}

bool AviEncoder::chunk(const RawFrameSlot& s, bool real, std::uint64_t core,
                       std::int32_t movie) noexcept {
    TASTY_SEAT_BODY(AviEncoder);
    pos_ = Position{};
    if (!active_ || s.gen != gen_ || broken_) return true;

    const bool plausible = avi::plausible_vtime(s.vtime);
    const bool same = plausible && cand_run_ != 0 && same_rate(s.vtime, cand_);
    const std::uint32_t cand = !plausible ? 0 : same ? cand_ : s.vtime;
    const std::uint32_t run = !plausible ? 0 : same ? cand_run_ + 1 : 1;
    const bool stable = run >= lim_.rate_frames;
    if (!kept_(core, movie)) {
        cand_ = cand;
        cand_run_ = run;
        if (seg_open_ && seg_rate_ != 0 && stable && !same_rate(cand, seg_rate_)) {
            seg_open_ = false;
            roll_rate_ = cand;
            ++st_.rate_rolls;
        } else if (!seg_open_ && stable) {
            roll_rate_ = cand;
        }
        return true;
    }
    codec_->arm(w_.clock, frame_budget_ns_(s.vtime));
    const bool ok = chunk_one_(*codec_, s, real, cand, run, stable);
    const IFrameCodec::Search se = codec_->search();
    st_.me_rad = se.radius;
    st_.me_cut = se.cut;
    return ok;
}

std::int64_t AviEncoder::frame_budget_ns_(std::uint32_t vtime) const noexcept {
    const std::uint32_t vt = avi::plausible_vtime(vtime) ? vtime : avi::kDefaultVtime;
    const std::int64_t period = std::int64_t{vt} * (1'000'000'000 / avi::kTickHz);
    return period * static_cast<std::int64_t>(lim_.budget_pct) / 100;
}

void AviEncoder::step_half_(bool behind, std::int64_t now) noexcept {
    scale_ = 2;
    need_roll_ = true;
    if (st_.steps != UINT8_MAX) ++st_.steps;
    st_.scale = 2;
    st_.fell_behind = behind ? 1 : 0;
    cause_ = behind && !too_soon_(now) ? HalfCause::Queue : HalfCause::Held;
    st_.held = cause_ == HalfCause::Held ? 1 : 0;
    calm_ns_ = now;
}

bool AviEncoder::too_soon_(std::int64_t now) const noexcept {
    return returned_ns_ >= 0 && now - returned_ns_ < kSustainWindows * lim_.budget_window_ns;
}

void AviEncoder::recover_(std::size_t queued, std::size_t depth, std::int64_t now) noexcept {
    if (scale_ != 2 || cause_ != HalfCause::Queue) return;
    if (queued * 4 > depth) {
        calm_ns_ = now;
        return;
    }
    const std::uint8_t doublings = std::min(st_.recovered, kRecoverDoublings);
    if (now - calm_ns_ < (lim_.recover_ns << doublings)) return;
    scale_ = 1;
    need_roll_ = true;
    cause_ = HalfCause::None;
    returned_ns_ = now;
    if (st_.recovered != UINT8_MAX) ++st_.recovered;
    st_.scale = 1;

    win_start_ = -1;
    win_over_ = 0;
}

void AviEncoder::budget_(std::int64_t cpu_ns, std::int64_t now) noexcept {
    if (win_start_ < 0) {
        win_start_ = now;
        win_cpu_ = 0;
    }
    win_cpu_ += cpu_ns;
    const std::int64_t span = now - win_start_;
    if (span < lim_.budget_window_ns || span <= 0) return;
    st_.budget_pct = static_cast<std::uint32_t>(win_cpu_ * 100 / span);
    if (st_.budget_pct > lim_.budget_pct) {
        if (win_over_ < kSustainWindows) ++win_over_;
    } else {
        win_over_ = 0;
    }
    if (step_ && scale_ == 1 && win_over_ >= kSustainWindows) step_half_(false, now);
    win_start_ = now;
    win_cpu_ = 0;
}

bool AviEncoder::size_roll_(std::size_t payload_max) const noexcept {
    if (seg_frames_ == 0) return false;
    const std::uint64_t index =
        avi::kChunkHead + avi::kIndexEntry * (std::uint64_t{seg_frames_} + 1u);
    return seg_frames_ >= std::min(lim_.segment_frames, kSegmentFrames) ||
           seg_bytes_ + avi::chunk_bytes(payload_max) + index > seg_limit_;
}

bool AviEncoder::open_segment_(std::uint32_t vtime) noexcept {
    flush_();
    if (!marker_(ChunkKind::Open, vtime)) return false;
    seg_open_ = true;
    seg_bytes_ = avi::kHeaderBytes;
    seg_frames_ = 0;
    seg_rate_ = vtime;
    need_key_ = true;
    st_.segment = seg_next_++;
    dirty_ = true;
    return true;
}

bool AviEncoder::marker_(ChunkKind kind, std::uint32_t vtime) noexcept {
    auto m = w_.out->acquire();
    if (!m) {
        ++st_.chunk_full;
        held_ = true;
        dirty_ = true;
        return false;
    }
    *m = ChunkSlot{};
    m->kind = kind;
    m->gen = gen_;
    if (kind == ChunkKind::Open) {
        m->segment = seg_next_;
        m->width = codec_->width();
        m->height = codec_->height();
        m->scale = scale_;
        m->codec = opt_.codec;
        m->frame_mul = frame_mul_;
        m->vtime = vtime;
        m->path = segment_path(sidecar_, seg_next_);
    }
    w_.out->send(std::move(m));
    return true;
}

AviEncoder::Reserve AviEncoder::reserve_chunks_(std::size_t need) noexcept {
    const std::size_t stripe = kDescBytes + kMinSlotBytes + need;
    constexpr std::size_t kBudget = kChunkSlots * (kDescBytes + kMinSlotBytes);
    const std::size_t depth = std::clamp(kBudget / stripe, kMinChunkDepth, kChunkSlots);
    if (!w_.out->set_live(static_cast<std::uint8_t>(depth))) return Reserve::Busy;
    if (auto r = chunks_.reserve(depth, stripe); !r) return Reserve::NoMemory;
    data_cap_ = chunks_.slot_bytes() - kDescBytes;
    st_.arena_kib = static_cast<std::uint32_t>(chunks_.bytes() / 1024u);
    dirty_ = true;
    return Reserve::Ok;
}

std::byte* AviEncoder::reserve_(std::size_t payload_max) noexcept {
    const std::size_t need = avi::chunk_bytes(payload_max);
    if (loan_ && (loan_->used + need > data_cap_ || loan_->chunks >= ChunkSlot::kMaxChunks))
        flush_();
    if (!loan_) {
        if (need > data_cap_) {

            if (!all_home_()) {
                ++st_.chunk_full;
                held_ = true;
                dirty_ = true;
                return nullptr;
            }
            if (reserve_chunks_(need) != Reserve::Ok) {

                ++st_.errors;
                data_cap_ = 0;
                broken_ = true;
                dirty_ = true;
                return nullptr;
            }
        }
        loan_ = w_.out->acquire();
        if (!loan_) {
            ++st_.chunk_full;
            held_ = true;
            dirty_ = true;
            return nullptr;
        }
        const std::span<std::byte> stripe = chunks_.stripe(w_.out->index_of(loan_));
        desc_ = reinterpret_cast<std::uint32_t*>(stripe.data());
        data_ = stripe.data() + kDescBytes;
        *loan_ = ChunkSlot{};
        loan_->kind = ChunkKind::Data;
        loan_->gen = gen_;
        loan_->bytes = data_;
        loan_->desc = desc_;
        loan_ns_ = now_();
    }
    return data_ + loan_->used + avi::kChunkHead;
}

void AviEncoder::commit_(std::size_t payload, bool key) noexcept {
    std::byte* const at = data_ + loan_->used;
    const auto len = static_cast<std::uint32_t>(payload);
    avi::chunk_head(len, std::span<std::byte, avi::kChunkHead>(at, avi::kChunkHead));
    if ((payload & 1u) != 0) at[avi::kChunkHead + payload] = std::byte{0};
    desc_[loan_->chunks++] = len | (key ? ChunkSlot::kKey : 0u);
    const std::size_t bytes = avi::chunk_bytes(payload);
    loan_->used += static_cast<std::uint32_t>(bytes);
    seg_bytes_ += bytes;
    ++seg_frames_;
    st_.bytes += bytes;
}

void AviEncoder::flush_() noexcept {
    if (!loan_) return;
    if (loan_->chunks != 0) w_.out->send(std::move(loan_));
    loan_ = ChunkChannel::Loan{};
    data_ = nullptr;
    desc_ = nullptr;
}

bool AviEncoder::all_home_() const noexcept {
    const auto c = w_.out->census();
    return c.idle == c.live;
}

void AviEncoder::tend() noexcept {
    TASTY_SEAT_BODY(AviEncoder);
    if (w_.out == nullptr) return;
    for (std::size_t i = 0; i < kChunkSlots; ++i) {
        auto back = w_.out->reap();
        if (!back) break;
    }
    if (loan_ && now_() - loan_ns_ >= kFlushNs) flush_();

    if (!active_ && !loan_ && chunks_.bytes() != 0 && all_home_()) {
        chunks_.release();
        data_cap_ = 0;
        st_.arena_kib = 0;
        dirty_ = true;
    }
}

int AviEncoder::park_ms() const noexcept {
    return loan_ || (!active_ && chunks_.bytes() != 0) ? static_cast<int>(kFlushNs / kMsNs) : -1;
}

void AviEncoder::release() noexcept {
    TASTY_SEAT_BODY(AviEncoder);
    flush_();
}

}  // namespace mister::app
