// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/zmbv_codec.h"

#include <zlib.h>

#include <cerrno>
#include <cstring>
#include <new>

namespace mister::app {
namespace {

constexpr std::size_t kAlign = 64;

constexpr std::size_t up(std::size_t n) noexcept { return (n + kAlign - 1) / kAlign * kAlign; }

z_stream* zs_of(std::byte* p) noexcept { return reinterpret_cast<z_stream*>(p); }

constexpr int kMeRung[] = {1, 2, 4, 8};

struct Cand {
    int dx;
    int dy;
};
constexpr Cand kSmall[] = {
    {-1, -1}, {0, -1}, {1, -1}, {-1, 0}, {1, 0},  {-1, 1}, {0, 1},  {1, 1},
    {-2, 0},  {2, 0},  {0, -2}, {0, 2},  {-4, 0}, {4, 0},  {0, -4}, {0, 4},
};

constexpr bool on_small_list(int dx, int dy) noexcept {
    if (dx == 0 && dy == 0) return true;
    for (const Cand& c : kSmall)
        if (c.dx == dx && c.dy == dy) return true;
    return false;
}

static_assert(sizeof kSmall / sizeof kSmall[0] == ZmbvCodec::kSmallCands);
static_assert(kMeRung[3] == ZmbvCodec::kMeRange);

}  // namespace

ZmbvCodec::~ZmbvCodec() { release_(); }

Ex<void> ZmbvCodec::begin(std::uint16_t w, std::uint16_t h) noexcept {
    TASTY_SEAT_BODY(ZmbvCodec);
    end();
    if (w == 0 || h == 0) return std::unexpected(Error{Errc::slot_range, ERR_SITE(), 0});
    bx_ = (static_cast<int>(w) + kBlock - 1) / kBlock;
    by_ = (static_cast<int>(h) + kBlock - 1) / kBlock;
    const std::size_t pic = std::size_t{w} * h * 4u;
    const std::size_t mv =
        (std::size_t(bx_) * static_cast<std::size_t>(by_) * 2u + 3u) & ~std::size_t{3};
    const std::size_t hist_n = std::size_t(bx_) * static_cast<std::size_t>(by_) * 2u;
    const std::size_t worst = mv + pic;

    const std::size_t bound = worst + ((worst + 7) >> 3) + ((worst + 63) >> 6) + 64u + 16u;
    const std::size_t zbytes = up(sizeof(z_stream));
    const std::size_t total =
        up(pic) * 2 + up(worst) + up(bound + 8) + zbytes + up(mv) + up(hist_n);
    if (auto r = arena_.reserve(1, total); !r) return r;
    std::byte* p = arena_.stripe(0).data();
    prev_ = p;
    p += up(pic);
    cur_ = p;
    p += up(pic);
    plain_ = p;
    p += up(worst);
    pkt_ = p;
    p += up(bound + 8);
    zmem_ = p;
    p += zbytes;
    dup_plain_ = p;
    dup_plain_len_ = mv;
    p += up(mv);
    hist_ = reinterpret_cast<std::int8_t*>(p);
    std::memset(hist_, 0, hist_n);
    width_ = w;
    height_ = h;
    max_payload_ = 7 + bound;
    auto* z = new (zmem_) z_stream{};
    if (::deflateInit(z, kLevel) != Z_OK) {
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
    if (z_ready_ && zmem_ != nullptr) {
        ::deflateEnd(zs_of(zmem_));
        z_ready_ = false;
    }
    arena_.release();
    prev_ = cur_ = plain_ = pkt_ = zmem_ = dup_plain_ = nullptr;
    hist_ = nullptr;
    dup_plain_len_ = 0;
    max_payload_ = 0;
    width_ = height_ = 0;
    bx_ = by_ = 0;
}

std::size_t ZmbvCodec::deflate_sync_(const std::byte* in, std::size_t n, std::byte* dst,
                                     std::size_t cap) noexcept {
    TASTY_SEAT_BODY(ZmbvCodec);
    if (!z_ready_ || cap > 0x7FFFFFFFu || n > 0x7FFFFFFFu) return 0;
    z_stream* z = zs_of(zmem_);
    z->next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(in));
    z->avail_in = static_cast<uInt>(n);
    z->next_out = reinterpret_cast<Bytef*>(dst);
    z->avail_out = static_cast<uInt>(cap);
    const int rc = ::deflate(z, Z_SYNC_FLUSH);
    if (rc != Z_OK || z->avail_in != 0) return 0;
    return cap - z->avail_out;
}

void ZmbvCodec::to_bgr0_(const std::byte* rgb, std::size_t line) noexcept {
    TASTY_SEAT_BODY(ZmbvCodec);
    auto* dst = reinterpret_cast<std::uint8_t*>(cur_);
    for (std::uint16_t y = 0; y < height_; ++y) {
        const auto* s = reinterpret_cast<const std::uint8_t*>(rgb + std::size_t{y} * line);
        for (std::uint16_t x = 0; x < width_; ++x) {
            dst[0] = s[2];
            dst[1] = s[1];
            dst[2] = s[0];
            dst[3] = 0;
            s += 3;
            dst += 4;
        }
    }
}

std::size_t ZmbvCodec::emit_key_(const std::byte* bgr0, std::span<std::byte> out) noexcept {
    TASTY_SEAT_BODY(ZmbvCodec);
    if (out.size() < max_payload_ || !z_ready_) return 0;
    if (::deflateReset(zs_of(zmem_)) != Z_OK) return 0;
    out[0] = std::byte{1};
    out[1] = std::byte{0};
    out[2] = std::byte{1};
    out[3] = std::byte{1};
    out[4] = std::byte{8};
    out[5] = std::byte{static_cast<unsigned char>(kBlock)};
    out[6] = std::byte{static_cast<unsigned char>(kBlock)};
    const std::size_t pic = std::size_t{width_} * height_ * 4u;
    const std::size_t n = deflate_sync_(bgr0, pic, out.data() + 7, out.size() - 7);
    return n == 0 ? 0 : 7 + n;
}

std::size_t ZmbvCodec::emit_inter_(const std::byte* plain, std::size_t n,
                                   std::span<std::byte> out) noexcept {
    TASTY_SEAT_BODY(ZmbvCodec);
    if (out.size() < max_payload_ || !z_ready_ || n == 0) return 0;
    out[0] = std::byte{0};
    const std::size_t got = deflate_sync_(plain, n, out.data() + 1, out.size() - 1);
    return got == 0 ? 0 : 1 + got;
}

std::size_t ZmbvCodec::build_inter_() noexcept {
    TASTY_SEAT_BODY(ZmbvCodec);
    const std::size_t mv_bytes =
        (std::size_t(bx_) * static_cast<std::size_t>(by_) * 2u + 3u) & ~std::size_t{3};
    std::memset(plain_, 0, mv_bytes);
    auto* mv = reinterpret_cast<std::int8_t*>(plain_);
    auto* xor_at = reinterpret_cast<std::uint8_t*>(plain_ + mv_bytes);
    const auto* cur = reinterpret_cast<const std::uint32_t*>(cur_);
    const auto* prev = reinterpret_cast<const std::uint32_t*>(prev_);
    const int W = width_;
    const bool watch = clock_ != nullptr && motion_ != RecMotion::Off && search_budget_ns_ > 0;
    bool cut = false;
    int block = 0;
    int bi = 0;
    for (int y = 0; y < height_; y += kBlock) {
        const int bh = height_ - y > kBlock ? kBlock : height_ - y;
        for (int x = 0; x < width_; x += kBlock, block += 2, ++bi) {
            const int bw = width_ - x > kBlock ? kBlock : width_ - x;
            if (watch && !cut && bi % kMeClockEvery == 0) {
                const std::int64_t elapsed = clock_->now().count() - frame_t0_;
                if (elapsed >= search_budget_ns_) cut = true;
            }
            const auto inside = [&](int dx, int dy) {
                return x + dx >= 0 && y + dy >= 0 && x + dx + bw <= W && y + dy + bh <= height_;
            };
            const auto sad = [&](int dx, int dy, std::uint32_t stop) {
                ++sad_calls_;
                std::uint32_t s = 0;
                for (int j = 0; j < bh; ++j) {
                    const auto* a = reinterpret_cast<const std::uint8_t*>(cur + (y + j) * W + x);
                    const auto* b =
                        reinterpret_cast<const std::uint8_t*>(prev + (y + dy + j) * W + (x + dx));
                    for (int i = 0; i < bw * 4; ++i) {
                        const int d = int{a[i]} - int{b[i]};
                        s += static_cast<std::uint32_t>(d < 0 ? -d : d);
                        if (s >= stop) return s;
                    }
                }
                return s;
            };
            int dx = 0;
            int dy = 0;
            std::uint32_t best = sad(0, 0, 0xFFFFFFFFu);
            const bool hunt = motion_ != RecMotion::Off && !cut && best != 0;
            if (hunt && radius_ <= 1) {
                for (int i = 0; i < kSmallCands && best != 0; ++i) {
                    const int ox = kSmall[i].dx;
                    const int oy = kSmall[i].dy;
                    if (!inside(ox, oy)) continue;
                    const std::uint32_t s = sad(ox, oy, best);
                    if (s < best) {
                        best = s;
                        dx = ox;
                        dy = oy;
                    }
                }
                if (best != 0 && hist_ != nullptr) {
                    const int px = hist_[block];
                    const int py = hist_[block + 1];
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
                            if ((ax > ay ? ax : ay) != rad || !inside(ox, oy)) continue;
                            const std::uint32_t s = sad(ox, oy, best);
                            if (s < best) {
                                best = s;
                                dx = ox;
                                dy = oy;
                            }
                            if (best == 0) break;
                        }
                    }
                }
            }
            if (hist_ != nullptr) {
                hist_[block] = static_cast<std::int8_t>(dx);
                hist_[block + 1] = static_cast<std::int8_t>(dy);
            }
            const bool xored = best != 0;
            mv[block] = static_cast<std::int8_t>((dx << 1) | (xored ? 1 : 0));
            mv[block + 1] = static_cast<std::int8_t>(dy << 1);
            if (!xored) continue;
            for (int j = 0; j < bh; ++j) {
                for (int i = 0; i < bw; ++i) {
                    const std::uint32_t v =
                        cur[(y + j) * W + x + i] ^ prev[(y + dy + j) * W + x + dx + i];
                    xor_at[0] = static_cast<std::uint8_t>(v);
                    xor_at[1] = static_cast<std::uint8_t>(v >> 8);
                    xor_at[2] = static_cast<std::uint8_t>(v >> 16);
                    xor_at[3] = static_cast<std::uint8_t>(v >> 24);
                    xor_at += 4;
                }
            }
        }
    }
    if (cut) {
        ++me_cut_;
        cut_frame_ = true;
    }
    return static_cast<std::size_t>(reinterpret_cast<std::byte*>(xor_at) - plain_);
}

std::size_t ZmbvCodec::encode(const std::byte* rgb, std::size_t line, bool key,
                              std::span<std::byte> out) noexcept {
    TASTY_SEAT_BODY(ZmbvCodec);
    if (!open() || rgb == nullptr) return 0;
    sad_calls_ = 0;
    cut_frame_ = false;
    const bool watch = clock_ != nullptr && motion_ != RecMotion::Off && search_budget_ns_ > 0;
    if (watch) frame_t0_ = clock_->now().count();
    to_bgr0_(rgb, line);
    std::int64_t post0 = 0;
    std::size_t n;
    if (key) {
        n = emit_key_(cur_, out);
    } else {
        const std::size_t plain_n = build_inter_();
        if (watch) post0 = clock_->now().count();
        n = emit_inter_(plain_, plain_n, out);
    }
    std::memcpy(prev_, cur_, std::size_t{width_} * height_ * 4u);
    if (watch) {
        const std::int64_t t1 = clock_->now().count();
        if (key) {
            if (!post_set_) note_post_(t1 - frame_t0_);
        } else {
            note_post_(t1 - post0);
        }
        if (motion_ == RecMotion::Auto) note_(t1 - frame_t0_, cut_frame_);
    }
    return n;
}

void ZmbvCodec::restart(const RecOptions& opt) noexcept {
    TASTY_SEAT_BODY(ZmbvCodec);
    const RecMotion motion = opt.motion;
    motion_ = motion;
    clock_ = nullptr;
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

void ZmbvCodec::arm(const os::IClock* clock, std::int64_t budget_ns) noexcept {
    TASTY_SEAT_BODY(ZmbvCodec);
    clock_ = clock;
    threshold_ns_ = budget_ns > 0 ? budget_ns : 0;
    std::int64_t room = threshold_ns_ - post_ns_;
    if (room < kMeSearchFloorNs) room = kMeSearchFloorNs;
    if (room > threshold_ns_) room = threshold_ns_;
    search_budget_ns_ = room;
}

void ZmbvCodec::note_post_(std::int64_t post) noexcept {
    TASTY_SEAT_BODY(ZmbvCodec);
    if (post <= 0) return;
    post_ns_ = post_set_ ? (post_ns_ * 3 + post) / 4 : post;
    post_set_ = true;
}

void ZmbvCodec::note_(std::int64_t spent, bool cut) noexcept {
    TASTY_SEAT_BODY(ZmbvCodec);
    if (spent < 0) spent = 0;
    const std::int64_t widen = threshold_ns_ * kMeWidenPct / 100;
    const std::int64_t narrow = threshold_ns_ * kMeNarrowPct / 100;
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
    if (kMeRung[idx] != radius_) idx = 0;
    idx += dir;
    if (idx < 0) idx = 0;
    if (idx >= n) idx = n - 1;
    radius_ = kMeRung[idx];
}

std::size_t ZmbvCodec::rekey(std::span<std::byte> out) noexcept {
    TASTY_SEAT_BODY(ZmbvCodec);
    if (!open()) return 0;
    return emit_key_(prev_, out);
}

std::span<const std::byte> ZmbvCodec::dup() noexcept {
    TASTY_SEAT_BODY(ZmbvCodec);
    if (!open() || pkt_ == nullptr) return {};
    const std::size_t n =
        emit_inter_(dup_plain_, dup_plain_len_, std::span<std::byte>(pkt_, max_payload_));
    if (n == 0) return {};
    return {pkt_, n};
}

}  // namespace mister::app
