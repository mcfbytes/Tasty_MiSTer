// SPDX-License-Identifier: GPL-3.0-or-later
#include "hal/scaler_probe.h"

#include <algorithm>
#include <array>
#include <span>

namespace mister::hal {
namespace {

bool plausible(const ScalerHeader& h) noexcept {
    constexpr unsigned kMinHeaderSize = 64;
    constexpr unsigned kMaxHeaderSize = 4096;
    constexpr unsigned kRgbBytesPerPixel = 3;
    const unsigned hs = h.header_size;
    return h.supported()
        && hs >= kMinHeaderSize
        && hs <= kMaxHeaderSize
        && (hs & (hs - 1u)) == 0u
        && h.width != 0
        && h.height != 0
        && static_cast<unsigned>(h.width) * kRgbBytesPerPixel <= h.line;
}

std::size_t frame_end(std::size_t base, const ScalerHeader& h) noexcept {
    return base + h.header_size + static_cast<std::size_t>(h.height) * h.line;
}

}

void ScalerProbe::restart(std::int64_t now) noexcept {
    sides_ = {ProbeSide{ScalerBuffers::kStrideLarge}, ProbeSide{ScalerBuffers::kStrideSmall}};
    start_ns_ = now;
}

void ScalerProbe::read(const ScalerBuffers& window) noexcept {
    for (ProbeSide& side : sides_) {
        for (std::size_t i = 0; i < side.buffers.size(); ++i) {
            ProbeBuffer& b = side.buffers[i];
            const auto h = window.header(i * side.stride);
            if (!h || !h->supported())
                continue;
            b.triple = h->triple_buffered();
            if (!b.seen) {
                b.first = h->frame_counter();
                b.seen = true;
            } else if (h->frame_counter() != b.first) {
                b.moved = true;
            }
        }
    }
}

ScalerProbe::Verdict ScalerProbe::decide(std::int64_t now) noexcept {
    const auto triple_live = [](const ProbeSide& p, std::size_t n) {
        std::size_t moved = 0;
        for (const ProbeBuffer& b : p.buffers) {
            if (!b.moved)
                continue;
            if (!b.triple)
                return false;
            ++moved;
        }
        return moved >= n;
    };
    const ProbeSide& large = sides_[0];
    if (triple_live(large, ScalerBuffers::kBuffers))
        return Verdict::Large;
    if (!large.buffers[0].triple && large.buffers[0].moved)
        return Verdict::LargeLowlat;
    if (now - start_ns_ < kProbeNs)
        return Verdict::Pending;
    constexpr std::uint32_t kLiveTriple = 2;
    if (triple_live(large, kLiveTriple))
        return Verdict::Large;
    if (triple_live(sides_[1], kLiveTriple))
        return Verdict::Small;
    return Verdict::Dead;
}

ScalerProbe::BufMask ScalerProbe::moved(std::size_t stride) const noexcept {

    const ProbeSide* side = &sides_[0];
    for (const ProbeSide& p : sides_)
        if (p.stride == stride)
            side = &p;
    BufMask out{};
    for (std::size_t i = 0; i < out.size(); ++i)
        out[i] = side->buffers[i].moved;
    return out;
}

namespace {
using Headers = std::array<ScalerHeader, ScalerBuffers::kBuffers>;
constexpr std::size_t kNoBuffer = ScalerBuffers::kBuffers;

std::size_t newest_buffer(const Headers& h, HeaderCtr::Lag lo, HeaderCtr::Lag hi) noexcept {
    for (std::size_t i = 0; i < ScalerBuffers::kBuffers; ++i) {
        bool ahead = true;
        for (std::size_t j = 0; j < ScalerBuffers::kBuffers; ++j) {
            if (j == i)
                continue;
            ahead = ahead && h[i].frame_counter().lead_over(h[j].frame_counter()).in_range(lo, hi);
        }
        if (ahead)
            return i;
    }
    return kNoBuffer;
}

struct WovenSpan {
    HeaderCtr::Lag lo;
    HeaderCtr::Lag hi;
};

inline constexpr std::array<WovenSpan, 2> kWovenSpans{{{{2}, {4}}, {{1}, {3}}}};
inline constexpr HeaderCtr::Lag kWovenMinLag{2};
inline constexpr HeaderCtr::Lag kOneFrame{1};

std::optional<ScalerProbe::Pick> woven_pick(const Headers& h, std::size_t top) noexcept {
    constexpr std::uint8_t kCounterModulus = HeaderCtr::kMask + 1;
    HeaderCtr::Lag lo{kCounterModulus};
    HeaderCtr::Lag hi{};
    std::size_t near = top;
    for (std::size_t j = 0; j < ScalerBuffers::kBuffers; ++j) {
        if (j == top)
            continue;
        const HeaderCtr::Lag d = h[top].frame_counter().lead_over(h[j].frame_counter());
        hi = std::max(hi, d);
        if (d < lo)
            lo = d;
        if (d.at_least(kWovenMinLag)
            && (near == top || d < h[top].frame_counter().lead_over(h[near].frame_counter())))
            near = j;
    }
    for (const WovenSpan& span : kWovenSpans) {
        if (lo != span.lo || hi != span.hi)
            continue;
        return ScalerProbe::Pick{
            .buf = near,
            .lag = h[top].frame_counter().lead_over(h[near].frame_counter()),
            .woven = true
        };
    }
    return std::nullopt;
}

std::optional<ScalerProbe::Pick> holding_pick(
    const Headers& h,
    const ScalerProbe::BufMask& live
) noexcept {
    const auto ctr = [&h](std::size_t i) {
        return h[i].frame_counter();
    };
    const auto holding = [&](HeaderCtr c) {
        std::size_t at = kNoBuffer;
        for (std::size_t i = 0; i < ScalerBuffers::kBuffers; ++i) {
            if (!live[i] || ctr(i) != c)
                continue;
            if (at != kNoBuffer)
                return kNoBuffer;
            at = i;
        }
        return at;
    };
    std::size_t done = kNoBuffer;
    for (std::size_t i = 0; i < ScalerBuffers::kBuffers; ++i) {
        if (!live[i] || holding(ctr(i)) != i || holding(ctr(i).plus(kOneFrame)) == kNoBuffer)
            continue;
        if (done == kNoBuffer || ctr(i).lead_over(ctr(done)) == kOneFrame)
            done = i;
    }
    if (done == kNoBuffer)
        return std::nullopt;
    return ScalerProbe::Pick{
        .buf = done,
        .lag = kOneFrame,
        .woven = false,
        .prev = holding(ctr(done).minus(kOneFrame))
    };
}

}

std::optional<ScalerProbe::Pick> ScalerProbe::pick_complete(
    const std::array<ScalerHeader, ScalerBuffers::kBuffers>& h,
    const BufMask& live
) noexcept {
    const bool all_live = std::all_of(live.begin(), live.end(), [](bool l) { return l; });
    for (std::size_t i = 0; i < ScalerBuffers::kBuffers; ++i)
        if (!all_live && live[i] && h[i].interlaced() && h[i].supported())
            return std::nullopt;
    constexpr HeaderCtr::Lag kAheadMin{1};
    constexpr HeaderCtr::Lag kAheadMax{4};
    if (const std::size_t top = all_live ? newest_buffer(h, kAheadMin, kAheadMax) : kNoBuffer;
        top != kNoBuffer && h[top].interlaced()) {
        if (auto woven = woven_pick(h, top))
            return woven;
    }
    return holding_pick(h, live);
}

std::optional<ScalerProbe::Verdict> ScalerProbe::still(
    const ScalerBuffers& window,
    std::optional<std::size_t> known
) noexcept {
    const auto head = window.header(0);
    if (!head || !plausible(*head))
        return std::nullopt;
    if (!head->triple_buffered())
        return Verdict::LargeLowlat;
    const bool small_first = known == ScalerBuffers::kStrideSmall;
    constexpr std::size_t kSides = 2;
    const std::array<std::size_t, kSides> order = small_first
        ? std::array{ScalerBuffers::kStrideSmall, ScalerBuffers::kStrideLarge}
        : std::array{ScalerBuffers::kStrideLarge, ScalerBuffers::kStrideSmall};
    for (const std::size_t stride : order) {
        std::array<ScalerHeader, ScalerBuffers::kBuffers> hs{};
        bool whole = true;
        for (std::size_t i = 0; i < hs.size() && whole; ++i) {
            const auto h = window.header(i * stride);
            whole = h
                && plausible(*h)
                && h->triple_buffered()
                && h->width == head->width
                && h->height == head->height
                && h->line == head->line
                && h->header_size == head->header_size;
            if (whole)
                hs[i] = *h;
        }
        if (whole && pick_complete(hs))
            return stride == ScalerBuffers::kStrideLarge ? Verdict::Large : Verdict::Small;
    }
    return std::nullopt;
}

bool ScalerProbe::port_stuck(ScalerBuffers& window) noexcept {
    static constexpr std::size_t kBeat = ScalerHeader::kBytes;
    constexpr std::size_t kMaxShortBeats = 255;
    constexpr std::size_t kChunkBytes = 4096;
    static constexpr std::size_t kMaxShort = kMaxShortBeats * kBeat;
    std::array<std::byte, kChunkBytes> chunk;
    const std::size_t len = window.len();
    for (std::size_t off = 0; off + chunk.size() <= len; off += chunk.size()) {
        if (!window.copy(off, chunk))
            return false;
        for (std::size_t i = 0; i < chunk.size(); i += kBeat) {
            const auto h = ScalerHeader::decode(
                std::span<const std::byte, ScalerHeader::kBytes>(chunk.data() + i, kBeat)
            );
            if (!plausible(h))
                continue;
            const std::size_t at = off + i;
            for (const std::size_t stride :
                 {ScalerBuffers::kStrideLarge, ScalerBuffers::kStrideSmall}) {
                const std::size_t base = at - at % stride;
                const std::size_t end = frame_end(base, h);
                if (end <= at || end - at > kMaxShort || (end - at) % kBeat != 0)
                    continue;
                if (const auto b = window.header(base); b && b->supported())
                    continue;
                const std::size_t large = at - at % ScalerBuffers::kStrideLarge;
                if (large != base) {
                    const auto l = window.header(large);
                    if (l && plausible(*l) && base < frame_end(large, *l))
                        continue;
                }
                return true;
            }
        }
    }
    return false;
}

}
