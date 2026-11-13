// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/avi_encoder.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <optional>
#include <utility>

#include "app/avi_format.h"
#include "app/log_clock.h"
#include "app/rgb_rows.h"
#include "infra/message_sum.h"

namespace mister::app {

namespace {

constexpr std::size_t kDescBytes = ChunkSlot::kMaxChunks * sizeof(std::uint32_t);
constexpr std::int64_t kMsNs = 1'000'000;

constexpr std::size_t kMinChunkDepth = 4;

}

std::int64_t AviEncoder::now_() const noexcept {
    return w_.clock ? w_.clock->now().time_since_epoch().count() : 0;
}

std::chrono::nanoseconds AviEncoder::cpu_now_() const noexcept {
    return w_.cpu ? w_.cpu->now().time_since_epoch() : std::chrono::nanoseconds{now_()};
}

bool AviEncoder::take_dirty() noexcept {
    return std::exchange(dirty_, false);
}

RecPath AviEncoder::segment_path(const RecPath& sidecar, SegmentIndex index) noexcept {
    const std::optional<RecordingName> name = RecordingName::from_sidecar(sidecar.view());
    return name ? name->segment(index) : RecPath{};
}

bool AviEncoder::open(const RawFrameSlot::Open& s, ControlGeneration gen) noexcept {
    constexpr std::size_t kBytesPerKib = 1024;
    TASTY_SEAT_BODY(AviEncoder);
    flush_();
    place_ = ChunkPlace::none();
    active_ = w_.out && s.mode == RecMode::Avi;
    if (!active_)
        return true;
    gen_ = gen;
    sidecar_ = s.path;
    opt_ = s.opt;
    codec_ = &codec_for_(opt_.codec);
    gov_.restart(opt_.scale);
    frame_mul_ = opt_.every;
    seg_limit_ = opt_.segment_bytes.empty() ? proto::FileSize{lim_.segment_bytes}
                                            : opt_.segment_bytes.min(kRecSegmentMax);
    broken_ = false;
    need_roll_ = false;
    seg_open_ = false;
    seg_next_ = SegmentIndex{};
    seg_rate_ = FrameTicks{};
    roll_rate_ = FrameTicks{};
    cand_ = FrameTicks{};
    cand_run_ = 0;
    need_key_ = true;
    since_key_ = 0;
    cscd_.end();
    zmbv_.end();
    st_ = EncodeStatus::Video{};
    st_.scale = gov_.scale();

    if (chunks_.bytes() == 0) {
        if (const auto got = reserve_chunks_(0); !got) {
            if (got.error() == Shortfall::Busy)
                return false;
            broken_ = true;
            data_cap_ = 0;
            dirty_ = true;
            if (!refuse_marker_())
                return false;
            ++st_.errors;
            return true;
        }
    }
    st_.arena_kib = static_cast<std::uint32_t>(chunks_.bytes() / kBytesPerKib);
    last_search_ = codec_->restart(opt_);
    st_.me_rad = last_search_.radius;
    st_.me_cut = last_search_.cut;
    dirty_ = true;
    return true;
}

bool AviEncoder::close(const RawFrameSlot::Close&, ControlGeneration gen) noexcept {
    TASTY_SEAT_BODY(AviEncoder);
    if (!active_ || gen != gen_)
        return true;
    flush_();
    if (!close_marker_())
        return false;
    active_ = false;
    seg_open_ = false;
    cscd_.end();
    zmbv_.end();
    half_.release();
    place_ = ChunkPlace::none();
    dirty_ = true;
    return true;
}

bool AviEncoder::kept_(WideCoreFrame core, MovieFrame movie) const noexcept {
    const RecOptions& o = opt_;
    if (o.from_frame >= MovieFrame{} || o.to_frame >= MovieFrame{}) {
        if (movie < MovieFrame{})
            return false;
        if (o.from_frame >= MovieFrame{} && movie < o.from_frame)
            return false;
        if (o.to_frame >= MovieFrame{} && movie >= o.to_frame)
            return false;
    }
    const std::uint32_t every = o.every.count<std::uint32_t>();
    if (every <= 1)
        return true;

    if (o.from_frame >= MovieFrame{})
        return (movie - o.from_frame).multiple_of(FrameCount{o.every.count<std::int32_t>()});
    return core.on_every(every);
}

IFrameCodec& AviEncoder::codec_for_(RecCodec c) noexcept {
    switch (c) {
        case RecCodec::Cscd:
            return cscd_;
        case RecCodec::Zmbv:
            return zmbv_;
        case RecCodec::kCount:
            break;
    }
    return cscd_;
}

PictureSize AviEncoder::picture_size_(const RawFrameSlot::Frame& s) const noexcept {
    const PictureSize full{s.width, s.rgb.rows};
    return full.halved_if(gov_.scale() == PictureScale::Half);
}

bool AviEncoder::chunk_one_(
    IFrameCodec& codec,
    const RawFrameSlot::Frame& s,
    bool real,
    FrameTicks cand,
    std::uint32_t run,
    bool stable
) noexcept {
    const PictureSize want = picture_size_(s);
    if (want.empty()) {
        ++st_.errors;
        dirty_ = true;
        return true;
    }
    if (codec.size().empty() || (real && want != codec.size())) {
        if (auto r = codec.begin(want); !r) {
            ++st_.errors;
            dirty_ = true;
            return true;
        }
        seg_open_ = false;
        seg_rate_ = FrameTicks{};
        roll_rate_ = FrameTicks{};
    }
    if (real && need_roll_) {
        seg_open_ = false;
        need_roll_ = false;
    }
    if (seg_open_ && seg_rate_.known() && stable && !cand.same_rate_as(seg_rate_)) {
        seg_open_ = false;
        roll_rate_ = cand;
        ++st_.rate_rolls;
    }
    const std::size_t pmax = codec.max_payload();
    if (seg_open_ && size_roll_(pmax))
        seg_open_ = false;
    if (!seg_open_ && !open_segment_(roll_rate_.known() ? roll_rate_ : seg_rate_))
        return false;
    roll_rate_ = FrameTicks{};
    const auto dst = reserve_(pmax);
    if (!dst)
        return dst.error() == Shortfall::NoMemory;
    const std::span<std::byte> out = *dst;
    std::span<const std::byte> payload;
    bool key = false;
    bool lost = false;
    if (real) {
        key = need_key_ || since_key_ >= lim_.key_interval;
        const std::chrono::nanoseconds c0 = cpu_now_();
        const RgbRows picture = gov_.scale() == PictureScale::Half ? half_.picture(s) : s.rgb;
        const FrameBudget budget = FrameBudget::of(w_.clock, s.rate, gov_.budget_pct());
        if (!picture.bytes.empty()) {
            const IFrameCodec::Encoded enc = codec.encode(picture, key, out, budget);
            payload = enc.payload;
            last_search_ = enc.search;
        }
        const std::chrono::nanoseconds spent = cpu_now_() - c0;
        st_.enc_us.note(us_of(spent));
        if (!payload.empty()) {
            ++st_.encoded;
            note_governor_(gov_.on_encoded(spent, std::chrono::nanoseconds{now_()}));
        }
        lost = picture.bytes.empty();
    }
    if (payload.empty()) {
        if (real) {
            ++st_.errors;
            need_key_ = true;
        }
        if (need_key_) {
            payload = codec.rekey(out);
            key = true;
        } else {
            const auto d = codec.dup();
            if (d.empty() || d.size() > pmax)
                return true;
            std::memcpy(dst->data(), d.data(), d.size());
            payload = out.first(d.size());
            key = false;
        }
        if (payload.empty())
            return true;
        ++st_.dups;
    }
    const AviFrameIndex index = AviFrameIndex{} + seg_frames_;
    commit_(payload.size(), key);
    cand_ = cand;
    cand_run_ = run;
    if (!seg_rate_.known() && stable)
        seg_rate_ = cand;
    if (seg_rate_.known())
        cur_.rate = seg_rate_;
    place_ = lost ? ChunkPlace::held(st_.segment) : ChunkPlace::written(st_.segment, index);
    if (key) {
        need_key_ = false;
        since_key_ = 0;
        ++st_.keys;
    } else {
        ++since_key_;
    }

    const std::size_t queued = w_.raw ? w_.raw->outbound() + 1 : 0;
    const std::size_t depth = s.depth != 0 ? s.depth : kRawFrameSlots;
    if (real)
        st_.queue_max = std::max<std::uint8_t>(st_.queue_max, static_cast<std::uint8_t>(queued));
    const bool held = held_;

    if (real
        && ScaleGovernor::QueueCount::of(queued).caught_up_to(ScaleGovernor::QueueCount::of(depth)))
        held_ = false;
    if (real && w_.raw)
        note_governor_(gov_.on_queue(
            ScaleGovernor::QueueCount::of(queued),
            ScaleGovernor::QueueCount::of(depth),
            held,
            std::chrono::nanoseconds{now_()}
        ));
    dirty_ = true;
    return true;
}

bool AviEncoder::chunk(
    const RawFrameSlot::Frame& s,
    ControlGeneration gen,
    bool real,
    WideCoreFrame core,
    MovieFrame movie
) noexcept {
    TASTY_SEAT_BODY(AviEncoder);
    place_ = ChunkPlace::none();
    if (!active_ || gen != gen_ || broken_)
        return true;

    const bool plausible = s.rate.known();
    const bool same = plausible && cand_run_ != 0 && s.rate.same_rate_as(cand_);
    const FrameTicks cand = !plausible ? FrameTicks::unknown() : same ? cand_ : s.rate;
    const std::uint32_t run = !plausible ? 0 : same ? cand_run_ + 1 : 1;
    const bool stable = run >= lim_.rate_frames;
    if (!kept_(core, movie)) {
        cand_ = cand;
        cand_run_ = run;
        if (seg_open_ && seg_rate_.known() && stable && !cand.same_rate_as(seg_rate_)) {
            seg_open_ = false;
            roll_rate_ = cand;
            ++st_.rate_rolls;
        } else if (!seg_open_ && stable) {
            roll_rate_ = cand;
        }
        return true;
    }
    const bool ok = chunk_one_(*codec_, s, real, cand, run, stable);
    st_.me_rad = last_search_.radius;
    st_.me_cut = last_search_.cut;
    return ok;
}

bool AviEncoder::chunk(
    const RawFrameSlot::Close& s,
    ControlGeneration gen,
    bool real,
    WideCoreFrame core,
    MovieFrame movie
) noexcept {
    RawFrameSlot::Frame view{};
    view.width = s.width;
    view.rgb = s.rgb;
    return chunk(view, gen, real, core, movie);
}

void AviEncoder::note_governor_(const ScaleGovernor::Change& ch) noexcept {
    if (ch.measured)
        st_.budget_pct = ch.budget_pct;
    if (ch.stepped) {
        st_.steps = gov_.steps();
        st_.fell_behind = gov_.fell_behind();
        st_.held = gov_.cause() == ScaleGovernor::Cause::Held;
    }
    if (ch.recovered)
        st_.recovered = gov_.recovered();
    if (!ch.changed)
        return;
    st_.scale = gov_.scale();
    need_roll_ = true;
}

bool AviEncoder::size_roll_(std::size_t payload_max) const noexcept {
    if (seg_frames_ == AviFrameCount{})
        return false;
    const std::uint64_t index = avi::kChunkHead
        + (seg_frames_ + AviFrameCount{1}).scaled_by(avi::kIndexEntry);
    return seg_frames_ >= std::min(lim_.segment_frames, kSegmentFrames)
        || proto::FileSize{seg_bytes_ + avi::chunk_bytes(payload_max) + index} > seg_limit_;
}

bool AviEncoder::open_segment_(FrameTicks rate) noexcept {
    flush_();
    if (!open_marker_(rate))
        return false;
    seg_open_ = true;
    seg_bytes_ = avi::kHeaderBytes;
    seg_frames_ = AviFrameCount{};
    seg_rate_ = rate;
    need_key_ = true;
    st_.segment = seg_next_;
    seg_next_ = seg_next_.next();
    dirty_ = true;
    return true;
}

template <class A>
bool AviEncoder::lend_(const A& alt) noexcept {
    auto m = w_.out->acquire();
    if (!m) {
        ++st_.chunk_full;
        held_ = true;
        dirty_ = true;
        return false;
    }
    *m = infra::make<ChunkSlot>(alt, gen_);
    w_.out->send(std::move(m));
    return true;
}

bool AviEncoder::open_marker_(FrameTicks rate) noexcept {
    return lend_(
        ChunkSlot::Open{
            .rate = rate,
            .size = codec_->size(),
            .segment = seg_next_,
            .frame_mul = frame_mul_,
            .scale = gov_.scale(),
            .codec = opt_.codec,
            .path = segment_path(sidecar_, seg_next_)
        }
    );
}

bool AviEncoder::close_marker_() noexcept {
    return lend_(ChunkSlot::Close{});
}

bool AviEncoder::refuse_marker_() noexcept {
    return lend_(ChunkSlot::Refuse{});
}

std::expected<void, AviEncoder::Shortfall> AviEncoder::reserve_chunks_(std::size_t need) noexcept {
    constexpr std::size_t kBytesPerKib = 1024;
    const std::size_t stripe = kDescBytes + kMinSlotBytes + need;
    constexpr std::size_t kBudget = kChunkSlots * (kDescBytes + kMinSlotBytes);
    const std::size_t depth = std::clamp(kBudget / stripe, kMinChunkDepth, kChunkSlots);

    if (const auto live = w_.out->set_live(static_cast<std::uint8_t>(depth)); !live)
        return std::unexpected(
            live.error().code == Errc::busy ? Shortfall::Busy : Shortfall::NoMemory
        );
    if (auto r = chunks_.reserve(depth, stripe); !r)
        return std::unexpected(Shortfall::NoMemory);
    data_cap_ = chunks_.slot_bytes() - kDescBytes;
    st_.arena_kib = static_cast<std::uint32_t>(chunks_.bytes() / kBytesPerKib);
    dirty_ = true;
    return {};
}

std::expected<std::span<std::byte>, AviEncoder::Shortfall> AviEncoder::reserve_(
    std::size_t payload_max
) noexcept {
    const std::size_t need = avi::chunk_bytes(payload_max);
    if (loan_ && (cur_.used + need > data_cap_ || cur_.chunks >= ChunkSlot::kMaxChunks))
        flush_();
    if (!loan_) {
        if (need > data_cap_) {

            if (!all_home_()) {
                ++st_.chunk_full;
                held_ = true;
                dirty_ = true;
                return std::unexpected(Shortfall::Busy);
            }
            if (!reserve_chunks_(need)) {

                ++st_.errors;
                data_cap_ = 0;
                broken_ = true;
                dirty_ = true;
                return std::unexpected(Shortfall::NoMemory);
            }
        }
        loan_ = w_.out->acquire();
        if (!loan_) {
            ++st_.chunk_full;
            held_ = true;
            dirty_ = true;
            return std::unexpected(Shortfall::Busy);
        }
        const std::span<std::byte> stripe = chunks_.stripe(*w_.out->index_of(loan_));
        desc_ = stripe.first(kDescBytes);
        data_ = stripe.subspan(kDescBytes);
        cur_ = ChunkSlot::Data{.bytes = data_, .desc = desc_};
        loan_ns_ = now_();
    }
    return data_.subspan(cur_.used + avi::kChunkHead, payload_max);
}

void AviEncoder::commit_(std::size_t payload, bool key) noexcept {
    std::byte* const at = data_.data() + cur_.used;
    const auto len = static_cast<std::uint32_t>(payload);
    avi::chunk_head(len, std::span<std::byte, avi::kChunkHead>(at, avi::kChunkHead));
    if ((payload & 1u) != 0)
        at[avi::kChunkHead + payload] = std::byte{0};
    ChunkSlot::set_desc_word(desc_, cur_.chunks++, ChunkDesc::of(len, key));
    const std::size_t bytes = avi::chunk_bytes(payload);
    cur_.used += static_cast<std::uint32_t>(bytes);
    seg_bytes_ += bytes;
    seg_frames_ += AviFrameCount{1};
    st_.bytes += bytes;
}

void AviEncoder::flush_() noexcept {
    if (!loan_)
        return;
    if (cur_.chunks != 0) {
        *loan_ = infra::make<ChunkSlot>(cur_, gen_);
        w_.out->send(std::move(loan_));
    }
    loan_ = ChunkChannel::Loan{};
    cur_ = ChunkSlot::Data{};
    data_ = {};
    desc_ = {};
}

bool AviEncoder::all_home_() const noexcept {
    const auto c = w_.out->census();
    return c.idle == c.live;
}

void AviEncoder::tend() noexcept {
    TASTY_SEAT_BODY(AviEncoder);
    if (!w_.out)
        return;
    for (std::size_t i = 0; i < kChunkSlots; ++i) {
        auto back = w_.out->reap();
        if (!back)
            break;
    }
    if (loan_ && now_() - loan_ns_ >= kFlushNs)
        flush_();

    if (!active_ && !loan_ && chunks_.bytes() != 0 && all_home_()) {
        chunks_.release();
        data_cap_ = 0;
        st_.arena_kib = 0;
        dirty_ = true;
    }
}

infra::ParkTimeout AviEncoder::park_ms() const noexcept {
    return loan_ || (!active_ && chunks_.bytes() != 0)
        ? std::chrono::duration_cast<infra::ParkTimeout>(std::chrono::nanoseconds{kFlushNs})
        : infra::kForever;
}

void AviEncoder::release() noexcept {
    TASTY_SEAT_BODY(AviEncoder);
    flush_();
}

}
