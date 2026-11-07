// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/zmbv_codec.h"

#include <zlib.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <limits>
#include <new>

#include "infra/byte_view.h"

namespace mister::app {
namespace {

constexpr std::size_t kAlign = 64;

constexpr std::size_t up(std::size_t n) noexcept {
    return (n + kAlign - 1) / kAlign * kAlign;
}

struct MvBlocks {
    std::size_t v = 0;

    [[nodiscard]] static constexpr MvBlocks of_grid(int cols, int rows) noexcept {
        return MvBlocks{static_cast<std::size_t>(cols) * static_cast<std::size_t>(rows)};
    }

    [[nodiscard]] constexpr std::size_t table_bytes() const noexcept {
        return v * ZmbvLayout::kMvBytesPerBlock;
    }

    [[nodiscard]] constexpr std::size_t padded_table_bytes() const noexcept {
        return (table_bytes() + ZmbvLayout::kMvAlignMask) & ~ZmbvLayout::kMvAlignMask;
    }
};

constexpr std::size_t deflate_bound(std::size_t n) noexcept {
    constexpr std::size_t kStoredBlockBytes = 8;
    constexpr std::size_t kHuffmanBlockBytes = 64;
    constexpr std::size_t kAllocFloor = 64;
    constexpr std::size_t kFlushMarker = 16;
    return n
        + (n + kStoredBlockBytes - 1) / kStoredBlockBytes
        + (n + kHuffmanBlockBytes - 1) / kHuffmanBlockBytes
        + kAllocFloor
        + kFlushMarker;
}

constexpr int kMeRung[] = {1, 2, 4, 8};

struct Cand {
    int dx;
    int dy;
};

constexpr Cand kSmall[] = {
    {-1, -1},
    {0, -1},
    {1, -1},
    {-1, 0},
    {1, 0},
    {-1, 1},
    {0, 1},
    {1, 1},
    {-2, 0},
    {2, 0},
    {0, -2},
    {0, 2},
    {-4, 0},
    {4, 0},
    {0, -4},
    {0, 4},
};

constexpr bool on_small_list(int dx, int dy) noexcept {
    if (dx == 0 && dy == 0)
        return true;
    for (const Cand& c : kSmall)
        if (c.dx == dx && c.dy == dy)
            return true;
    return false;
}

static_assert(sizeof kSmall / sizeof kSmall[0] == ZmbvCodec::kSmallCands);
static_assert(kMeRung[3] == ZmbvCodec::kMeRange);

}

ZmbvCodec::~ZmbvCodec() {
    release_();
}

Ex<void> ZmbvCodec::begin(std::uint16_t w, std::uint16_t h) noexcept {
    constexpr std::size_t kStripePlanes = 2;
    constexpr std::size_t kDeflateStreamSlack = 8;
    TASTY_SEAT_BODY(ZmbvCodec);
    end();
    if (w == 0 || h == 0)
        return std::unexpected(Error{Errc::slot_range, ERR_SITE(), 0});
    bx_ = (static_cast<int>(w) + kBlock - 1) / kBlock;
    by_ = (static_cast<int>(h) + kBlock - 1) / kBlock;
    const std::size_t pic = std::size_t{w} * h * ZmbvLayout::kPixelBytes;
    const MvBlocks blocks = MvBlocks::of_grid(bx_, by_);
    const std::size_t mv = blocks.padded_table_bytes();
    const std::size_t hist_n = blocks.table_bytes();
    const std::size_t worst = mv + pic;
    const std::size_t bound = deflate_bound(worst);
    const std::size_t total = up(pic) * kStripePlanes
        + up(worst)
        + up(bound + kDeflateStreamSlack)
        + up(mv)
        + up(hist_n);
    if (auto r = arena_.reserve(1, total); !r)
        return r;
    std::span<std::byte> rest = arena_.stripe(0);
    const auto carve = [&rest](std::size_t n) {
        const std::span<std::byte> s = rest.first(n);
        rest = rest.subspan(n);
        return s;
    };
    prev_ = carve(up(pic));
    cur_ = carve(up(pic));
    plain_ = carve(up(worst));
    pkt_ = carve(up(bound + kDeflateStreamSlack));
    dup_plain_ = carve(up(mv));
    dup_plain_len_ = mv;
    const std::span<std::byte> hist = carve(up(hist_n));
    hist_ = hist.first(hist_n);
    std::fill(hist_.begin(), hist_.end(), std::byte{0});
    width_ = w;
    height_ = h;
    max_payload_ = ZmbvLayout::kKeyHeaderBytes + bound;
    z_ = z_stream{};
    if (::deflateInit(&z_, kLevel) != Z_OK) {
        end();
        return std::unexpected(Error{Errc::slot_range, ERR_SITE(), 0});
    }
    z_ready_ = true;

    return {};
}

void ZmbvCodec::end() noexcept {
    TASTY_SEAT_BODY(ZmbvCodec);
    release_();
}

void ZmbvCodec::release_() noexcept {
    if (z_ready_) {
        ::deflateEnd(&z_);
        z_ready_ = false;
    }
    arena_.release();
    prev_ = cur_ = plain_ = pkt_ = dup_plain_ = {};
    hist_ = {};
    dup_plain_len_ = 0;
    max_payload_ = 0;
    width_ = height_ = 0;
    bx_ = by_ = 0;
}

std::size_t ZmbvCodec::deflate_sync_(
    const std::byte* in,
    std::size_t n,
    std::byte* dst,
    std::size_t cap
) noexcept {
    constexpr std::size_t kInt31Max = 0x7FFF'FFFFu;
    TASTY_SEAT_BODY(ZmbvCodec);
    if (!z_ready_ || cap > kInt31Max || n > kInt31Max)
        return 0;
    z_stream* z = &z_;
    z->next_in = const_cast<Bytef*>(
        infra::byte_cast<const std::uint8_t>(std::span<const std::byte>(in, n)).data()
    );
    z->avail_in = static_cast<uInt>(n);
    z->next_out = infra::byte_cast<std::uint8_t>(std::span<std::byte>(dst, cap)).data();
    z->avail_out = static_cast<uInt>(cap);
    const int rc = ::deflate(z, Z_SYNC_FLUSH);
    if (rc != Z_OK || z->avail_in != 0)
        return 0;
    return cap - z->avail_out;
}

void ZmbvCodec::to_bgr0_(const std::byte* rgb, std::size_t line) noexcept {
    constexpr std::size_t kBgr0Bytes = ZmbvLayout::kPixelBytes;
    TASTY_SEAT_BODY(ZmbvCodec);
    auto* dst = infra::byte_cast<std::uint8_t>(cur_).data();
    for (std::uint16_t y = 0; y < height_; ++y) {
        const auto* s = infra::byte_cast<const std::uint8_t>(std::span<const std::byte>(
                                                                 rgb + std::size_t{y} * line,
                                                                 static_cast<std::size_t>(width_)
                                                                     * kRgbPixelBytes
                                                             ))
                            .data();
        for (std::uint16_t x = 0; x < width_; ++x) {
            dst[0] = s[2];
            dst[1] = s[1];
            dst[2] = s[0];
            dst[3] = 0;
            s += kRgbPixelBytes;
            dst += kBgr0Bytes;
        }
    }
}

std::size_t ZmbvCodec::emit_key_(const std::byte* bgr0, std::span<std::byte> out) noexcept {
    constexpr std::size_t kBgr0Bytes = ZmbvLayout::kPixelBytes;
    constexpr std::size_t kKeyHeaderBytes = ZmbvLayout::kKeyHeaderBytes;
    TASTY_SEAT_BODY(ZmbvCodec);
    if (out.size() < max_payload_ || !z_ready_)
        return 0;
    if (::deflateReset(&z_) != Z_OK)
        return 0;
    out[0] = std::byte{ZmbvLayout::kKeyframeFlag};
    out[1] = std::byte{ZmbvLayout::kVersionMajor};
    out[2] = std::byte{ZmbvLayout::kVersionMinor};
    out[3] = std::byte{ZmbvLayout::kCompressionZlib};
    out[4] = std::byte{ZmbvLayout::kFormatTopDownBgr0};
    out[5] = std::byte{static_cast<unsigned char>(kBlock)};
    out[6] = std::byte{static_cast<unsigned char>(kBlock)};
    const std::size_t pic = std::size_t{width_} * height_ * kBgr0Bytes;
    const std::size_t
        n = deflate_sync_(bgr0, pic, out.data() + kKeyHeaderBytes, out.size() - kKeyHeaderBytes);
    return n == 0 ? 0 : kKeyHeaderBytes + n;
}

std::size_t ZmbvCodec::emit_inter_(
    const std::byte* plain,
    std::size_t n,
    std::span<std::byte> out
) noexcept {
    constexpr std::size_t kInterHeaderBytes = ZmbvLayout::kInterHeaderBytes;
    TASTY_SEAT_BODY(ZmbvCodec);
    if (out.size() < max_payload_ || !z_ready_ || n == 0)
        return 0;
    out[0] = std::byte{0};
    const std::size_t got = deflate_sync_(
        plain,
        n,
        out.data() + kInterHeaderBytes,
        out.size() - kInterHeaderBytes
    );
    return got == 0 ? 0 : kInterHeaderBytes + got;
}

std::size_t ZmbvCodec::build_inter_() noexcept {
    constexpr std::size_t kBgr0Bytes = ZmbvLayout::kPixelBytes;
    constexpr int kBgr0Lanes = static_cast<int>(ZmbvLayout::kPixelBytes);
    constexpr std::uint32_t kSadNoStop = std::numeric_limits<std::uint32_t>::max();
    TASTY_SEAT_BODY(ZmbvCodec);
    const std::size_t mv_bytes = MvBlocks::of_grid(bx_, by_).padded_table_bytes();
    std::memset(plain_.data(), 0, mv_bytes);
    std::byte* mv = plain_.data();
    auto* const plain_bytes = infra::byte_cast<std::uint8_t>(plain_).data();
    auto* xor_at = plain_bytes + mv_bytes;
    const auto cur = infra::byte_cast<const std::uint8_t>(std::span<const std::byte>(cur_));
    const auto prev = infra::byte_cast<const std::uint8_t>(std::span<const std::byte>(prev_));
    const int W = width_;
    const bool watch = clock_.has_value() && motion_ != RecMotion::Off && search_budget_ns_ > 0;
    bool cut = false;
    int block = 0;
    int bi = 0;
    for (int y = 0; y < height_; y += kBlock) {
        const int bh = height_ - y > kBlock ? kBlock : height_ - y;
        for (int x = 0; x < width_; x += kBlock, block += 2, ++bi) {
            const int bw = width_ - x > kBlock ? kBlock : width_ - x;
            if (watch && !cut && bi % kMeClockEvery == 0) {
                const std::int64_t elapsed = clock_->now().count() - frame_t0_;
                if (elapsed >= search_budget_ns_)
                    cut = true;
            }
            const auto inside = [&](int dx, int dy) {
                return x + dx >= 0 && y + dy >= 0 && x + dx + bw <= W && y + dy + bh <= height_;
            };
            const auto sad = [&](int dx, int dy, std::uint32_t stop) {
                ++sad_calls_;
                std::uint32_t s = 0;
                for (int j = 0; j < bh; ++j) {
                    const auto* a = cur.data() + std::size_t((y + j) * W + x) * kBgr0Bytes;
                    const auto* b = prev.data()
                        + std::size_t((y + dy + j) * W + (x + dx)) * kBgr0Bytes;
                    for (int i = 0; i < bw * kBgr0Lanes; ++i) {
                        const int d = int{a[i]} - int{b[i]};
                        s += static_cast<std::uint32_t>(d < 0 ? -d : d);
                        if (s >= stop)
                            return s;
                    }
                }
                return s;
            };
            int dx = 0;
            int dy = 0;
            std::uint32_t best = sad(0, 0, kSadNoStop);
            const bool hunt = motion_ != RecMotion::Off && !cut && best != 0;
            if (hunt && radius_ <= 1) {
                for (int i = 0; i < kSmallCands && best != 0; ++i) {
                    const int ox = kSmall[i].dx;
                    const int oy = kSmall[i].dy;
                    if (!inside(ox, oy))
                        continue;
                    const std::uint32_t s = sad(ox, oy, best);
                    if (s < best) {
                        best = s;
                        dx = ox;
                        dy = oy;
                    }
                }
                if (best != 0 && !hist_.empty()) {
                    const auto hb = static_cast<std::size_t>(block);
                    const int px = static_cast<std::int8_t>(hist_[hb]);
                    const int py = static_cast<std::int8_t>(hist_[hb + 1]);
                    if (!on_small_list(px, py) && inside(px, py)) {
                        const std::uint32_t s = sad(px, py, best);
                        if (s < best) {
                            best = s;
                            dx = px;
                            dy = py;
                        }
                    }
                }
            } else if (hunt) {
                const int cap = radius_ < kMeRange ? radius_ : kMeRange;
                for (int rad = 1; rad <= cap && best != 0; ++rad) {
                    for (int oy = -rad; oy <= rad && best != 0; ++oy) {
                        for (int ox = -rad; ox <= rad; ++ox) {
                            const int ax = ox < 0 ? -ox : ox;
                            const int ay = oy < 0 ? -oy : oy;
                            if ((ax > ay ? ax : ay) != rad || !inside(ox, oy))
                                continue;
                            const std::uint32_t s = sad(ox, oy, best);
                            if (s < best) {
                                best = s;
                                dx = ox;
                                dy = oy;
                            }
                            if (best == 0)
                                break;
                        }
                    }
                }
            }
            if (!hist_.empty()) {
                const auto hb = static_cast<std::size_t>(block);
                hist_[hb] = std::byte{static_cast<unsigned char>(dx)};
                hist_[hb + 1] = std::byte{static_cast<unsigned char>(dy)};
            }
            const bool xored = best != 0;
            mv[block] = std::byte{static_cast<unsigned char>((dx << 1) | (xored ? 1 : 0))};
            mv[block + 1] = std::byte{static_cast<unsigned char>(dy << 1)};
            if (!xored)
                continue;
            for (int j = 0; j < bh; ++j) {
                for (int i = 0; i < bw; ++i) {
                    const auto* a = cur.data() + std::size_t((y + j) * W + x + i) * kBgr0Bytes;
                    const auto* b = prev.data()
                        + std::size_t((y + dy + j) * W + x + dx + i) * kBgr0Bytes;
                    for (int k = 0; k < kBgr0Lanes; ++k)
                        xor_at[k] = static_cast<std::uint8_t>(a[k] ^ b[k]);
                    xor_at += kBgr0Bytes;
                }
            }
        }
    }
    if (cut) {
        ++me_cut_;
        cut_frame_ = true;
    }
    return static_cast<std::size_t>(xor_at - plain_bytes);
}

std::span<const std::byte> ZmbvCodec::encode(
    RgbRows rgb,
    bool key,
    std::span<std::byte> out
) noexcept {
    constexpr std::size_t kBgr0Bytes = ZmbvLayout::kPixelBytes;
    TASTY_SEAT_BODY(ZmbvCodec);
    if (!open() || !rgb.covers(rgb_row_of(width_), PictureRows{height_}))
        return {};
    sad_calls_ = 0;
    cut_frame_ = false;
    const bool watch = clock_.has_value() && motion_ != RecMotion::Off && search_budget_ns_ > 0;
    if (watch)
        frame_t0_ = clock_->now().count();
    to_bgr0_(rgb.bytes.data(), rgb.stride.v);
    std::int64_t post0 = 0;
    std::size_t n;
    if (key) {
        n = emit_key_(cur_.data(), out);
    } else {
        const std::size_t plain_n = build_inter_();
        if (watch)
            post0 = clock_->now().count();
        n = emit_inter_(plain_.data(), plain_n, out);
    }
    std::memcpy(prev_.data(), cur_.data(), std::size_t{width_} * height_ * kBgr0Bytes);
    if (watch) {
        const std::int64_t t1 = clock_->now().count();
        if (key) {
            if (!post_set_)
                note_post_(t1 - frame_t0_);
        } else {
            note_post_(t1 - post0);
        }
        if (motion_ == RecMotion::Auto)
            note_(t1 - frame_t0_, cut_frame_);
    }
    return out.first(n);
}

void ZmbvCodec::restart(const RecOptions& opt) noexcept {
    TASTY_SEAT_BODY(ZmbvCodec);
    const RecMotion motion = opt.motion;
    motion_ = motion;
    clock_.reset();
    threshold_ns_ = 0;
    search_budget_ns_ = 0;
    post_ns_ = 0;
    post_set_ = false;
    widen_run_ = 0;
    cut_frame_ = false;
    me_cut_ = 0;
    sad_calls_ = 0;
    switch (motion) {
        case RecMotion::Off:
            radius_ = 0;
            break;
        case RecMotion::Full:
            radius_ = kMeRange;
            break;
        case RecMotion::Small:
        case RecMotion::Auto:
            radius_ = 1;
            break;
    }
}

void ZmbvCodec::arm(infra::OptRef<const os::IClock> clock, std::int64_t budget_ns) noexcept {
    TASTY_SEAT_BODY(ZmbvCodec);
    clock_ = clock;
    threshold_ns_ = budget_ns > 0 ? budget_ns : 0;
    std::int64_t room = threshold_ns_ - post_ns_;
    if (room < kMeSearchFloorNs)
        room = kMeSearchFloorNs;
    if (room > threshold_ns_)
        room = threshold_ns_;
    search_budget_ns_ = room;
}

void ZmbvCodec::note_post_(std::int64_t post) noexcept {
    constexpr std::int64_t kPostWeightOld = 3;
    constexpr std::int64_t kPostWeightSum = 4;
    TASTY_SEAT_BODY(ZmbvCodec);
    if (post <= 0)
        return;
    post_ns_ = post_set_ ? (post_ns_ * kPostWeightOld + post) / kPostWeightSum : post;
    post_set_ = true;
}

void ZmbvCodec::note_(std::int64_t spent, bool cut) noexcept {
    constexpr std::int64_t kPercent = 100;
    TASTY_SEAT_BODY(ZmbvCodec);
    if (spent < 0)
        spent = 0;
    const std::int64_t widen = threshold_ns_ * kMeWidenPct / kPercent;
    const std::int64_t narrow = threshold_ns_ * kMeNarrowPct / kPercent;
    if (cut || spent > narrow) {
        widen_run_ = 0;
        step_(-1);
        return;
    }
    if (spent < widen) {
        if (++widen_run_ >= kMeWidenRun) {
            widen_run_ = 0;
            step_(1);
        }
        return;
    }
    widen_run_ = 0;
}

void ZmbvCodec::step_(int dir) noexcept {
    TASTY_SEAT_BODY(ZmbvCodec);
    constexpr int n = static_cast<int>(sizeof kMeRung / sizeof kMeRung[0]);
    int idx = 0;
    while (idx + 1 < n && kMeRung[idx] < radius_)
        ++idx;
    if (kMeRung[idx] != radius_)
        idx = 0;
    idx += dir;
    if (idx < 0)
        idx = 0;
    if (idx >= n)
        idx = n - 1;
    radius_ = kMeRung[idx];
}

std::span<const std::byte> ZmbvCodec::rekey(std::span<std::byte> out) noexcept {
    TASTY_SEAT_BODY(ZmbvCodec);
    if (!open())
        return {};
    return out.first(emit_key_(prev_.data(), out));
}

std::span<const std::byte> ZmbvCodec::dup() noexcept {
    TASTY_SEAT_BODY(ZmbvCodec);
    if (!open() || pkt_.empty())
        return {};
    const std::size_t n = emit_inter_(dup_plain_.data(), dup_plain_len_, pkt_.first(max_payload_));
    if (n == 0)
        return {};
    return {pkt_.data(), n};
}

}
