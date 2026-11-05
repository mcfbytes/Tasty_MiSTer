// SPDX-License-Identifier: GPL-3.0-or-later
#include "hal/scaler_probe.h"

#include <algorithm>
#include <array>
#include <span>

namespace mister::hal {
namespace {

constexpr std::uint8_t ctr_minus(std::uint8_t c, std::uint32_t n) noexcept {
    constexpr std::uint32_t kCounterMask = 0x7;
    return static_cast<std::uint8_t>((c - n) & kCounterMask);
}

constexpr std::uint8_t ctr_plus(std::uint8_t c, std::uint32_t n) noexcept {
    constexpr std::uint32_t kCounterMask = 0x7;
    return static_cast<std::uint8_t>((c + n) & kCounterMask);
}

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
    sides_ = {};
    start_ns_ = now;
}

void ScalerProbe::read(const ScalerBuffers& window) noexcept {
    constexpr std::size_t kSides = 2;
    for (std::size_t side = 0; side < kSides; ++side) {
        ProbeSide& p = sides_[side];
        const std::size_t stride = side == 0 ? ScalerBuffers::kStrideLarge
                                             : ScalerBuffers::kStrideSmall;
        for (std::size_t i = 0; i < ScalerBuffers::kBuffers; ++i) {
            const auto h = window.header(i * stride);
            if (!h || !h->supported())
                continue;
            p.triple[i] = h->triple_buffered();
            if (!p.seen[i]) {
                p.first[i] = h->frame_counter();
                p.seen[i] = true;
            } else if (h->frame_counter() != p.first[i]) {
                p.moved[i] = true;
            }
        }
    }
}

ScalerProbe::Verdict ScalerProbe::decide(std::int64_t now) noexcept {
    const auto triple_live = [](const ProbeSide& p, std::size_t n) {
        std::size_t moved = 0;
        for (std::size_t i = 0; i < ScalerBuffers::kBuffers; ++i) {
            if (!p.moved[i])
                continue;
            if (!p.triple[i])
                return false;
            ++moved;
        }
        return moved >= n;
    };
    const ProbeSide& large = sides_[0];
    if (triple_live(large, ScalerBuffers::kBuffers))
        return Verdict::Large;
    if (!large.triple[0] && large.moved[0])
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
    return sides_[stride == ScalerBuffers::kStrideSmall ? 1 : 0].moved;
}

std::optional<ScalerProbe::Pick> ScalerProbe::pick_complete(
    const std::array<ScalerHeader, ScalerBuffers::kBuffers>& h,
    const BufMask& live
) noexcept {
    const bool all_live = std::all_of(live.begin(), live.end(), [](bool l) { return l; });
    const auto newest = [&h](std::uint32_t lo, std::uint32_t hi) {
        for (std::size_t i = 0; i < ScalerBuffers::kBuffers; ++i) {
            bool ahead = true;
            for (std::size_t j = 0; j < ScalerBuffers::kBuffers; ++j) {
                if (j == i)
                    continue;
                const auto d = ctr_minus(h[i].frame_counter(), h[j].frame_counter());
                ahead = ahead && d >= lo && d <= hi;
            }
            if (ahead)
                return i;
        }
        return ScalerBuffers::kBuffers;
    };
    for (std::size_t i = 0; i < ScalerBuffers::kBuffers; ++i)
        if (!all_live && live[i] && h[i].interlaced() && h[i].supported())
            return std::nullopt;
    constexpr std::uint32_t kAheadMin = 1;
    constexpr std::uint32_t kAheadMax = 4;
    constexpr std::uint8_t kCounterModulus = 8;

    enum : std::uint8_t {
        kWovenLoWide = 2,
        kWovenHiWide = 4,
        kWovenLoNarrow = 1,
        kWovenHiNarrow = 3
    };

    constexpr std::uint8_t kWovenMinLag = 2;
    if (const std::size_t top = all_live ? newest(kAheadMin, kAheadMax) : ScalerBuffers::kBuffers;
        top != ScalerBuffers::kBuffers && h[top].interlaced()) {
        std::uint8_t lo = kCounterModulus;
        std::uint8_t hi = 0;
        std::size_t near = top;
        for (std::size_t j = 0; j < ScalerBuffers::kBuffers; ++j) {
            if (j == top)
                continue;
            const auto d = ctr_minus(h[top].frame_counter(), h[j].frame_counter());
            hi = std::max(hi, d);
            if (d < lo)
                lo = d;
            if (d >= kWovenMinLag
                && (near == top || d < ctr_minus(h[top].frame_counter(), h[near].frame_counter())))
                near = j;
        }
        if ((lo == kWovenLoWide && hi == kWovenHiWide)
            || (lo == kWovenLoNarrow && hi == kWovenHiNarrow))
            return Pick{
                .buf = near,
                .lag = ctr_minus(h[top].frame_counter(), h[near].frame_counter()),
                .woven = true
            };
    }
    constexpr std::size_t kNone = ScalerBuffers::kBuffers;
    const auto ctr = [&h](std::size_t i) {
        return h[i].frame_counter();
    };
    const auto holding = [&](std::uint8_t c) {
        std::size_t at = kNone;
        for (std::size_t i = 0; i < ScalerBuffers::kBuffers; ++i) {
            if (!live[i] || ctr(i) != c)
                continue;
            if (at != kNone)
                return kNone;
            at = i;
        }
        return at;
    };
    std::size_t done = kNone;
    for (std::size_t i = 0; i < ScalerBuffers::kBuffers; ++i) {
        if (!live[i] || holding(ctr(i)) != i || holding(ctr_plus(ctr(i), 1)) == kNone)
            continue;
        if (done == kNone || ctr_minus(ctr(i), ctr(done)) == 1)
            done = i;
    }
    if (done == kNone)
        return std::nullopt;
    return Pick{.buf = done, .lag = 1, .woven = false, .prev = holding(ctr_minus(ctr(done), 1))};
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
