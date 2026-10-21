// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/osd_renderer.h"

#include <algorithm>
#include <array>
#include <cstring>

#include "app/link_tx_channel.h"
#include "ui/glyph_rom.h"
#include "ui/status_bar_facts.h"

namespace mister::ui {

std::size_t format_key_value(std::span<char> out, std::string_view name, std::string_view value) {
    const std::size_t w = kOptionRowWidth;
    if (out.size() < w + 1)
        return 0;

    std::fill(out.begin(), out.begin() + static_cast<std::ptrdiff_t>(w), ' ');
    out[w] = '\0';

    std::size_t n = std::min(name.size(), w - 1);
    while (n > 1 && name[n - 1] == ' ')
        --n;
    for (std::size_t i = 0; i < n; ++i)
        out[i + 1] = name[i];
    std::size_t len = n + 1;

    const std::size_t vlen = std::min(value.size(), w);

    if (len + vlen > w - 1) {
        len = (vlen < w - 1) ? (w - 1 - vlen) : 1;
    }
    out[len] = ':';
    ++len;

    const std::size_t pad_to = (w > vlen) ? (w - vlen) : 0;
    for (std::size_t i = len; i < pad_to; ++i)
        out[i] = ' ';
    for (std::size_t i = 0; i < vlen; ++i)
        out[pad_to + i] = value[i];

    return w;
}

namespace {

constexpr unsigned kLineBytes = proto::OsdSurface::kRowBytes;

constexpr char kEscStipple = 0x0B;
constexpr char kEscXorChar = 0x0C;

[[nodiscard]] std::array<std::uint8_t, 8> rotate_char(const std::uint8_t* in) noexcept {
    std::array<std::uint8_t, 8> out{};
    for (int b = 0; b < 8; ++b) {
        int a = 0;
        for (int c = 0; c < 8; ++c) {
            a <<= 1;
            a |= (in[c] >> b) & 1;
        }
        out[static_cast<std::size_t>(b)] = static_cast<std::uint8_t>(a);
    }
    return out;
}

}

void OsdRenderer::set_title(std::string_view s, Arrow arrow) noexcept {
    arrow_ = arrow;

    unsigned osd_height = height_.v * 8;
    if (osd_height < 16)
        osd_height = 16;
    if (osd_height > sizeof title_)
        osd_height = sizeof title_;
    std::fill(std::begin(title_), std::end(title_), std::uint8_t{0});
    unsigned zeros = 0;
    unsigned outp = 0;
    for (const char ch : s) {
        const auto c = static_cast<unsigned char>(ch);
        if (c == 0 || outp >= osd_height - 8)
            break;
        const std::uint8_t* p = kGlyphRom[c];
        for (unsigned j = 0; j < 8; ++j) {
            const std::uint8_t nc = p[j];
            if (nc != 0) {
                zeros = 0;
                title_[outp++] = nc;
            } else if (zeros == 0 || (c == ' ' && zeros < 5)) {
                title_[outp++] = 0;
                ++zeros;
            }
            if (outp >= sizeof title_)
                break;
        }
    }
    for (unsigned i = outp; i < osd_height; ++i)
        title_[i] = 0;

    const unsigned c = (outp + 1 < osd_height) ? ((osd_height - 1 - outp) / 2) : 0u;
    std::memmove(title_ + c, title_, outp);
    for (unsigned i = 0; i < c; ++i)
        title_[i] = 0;

    for (unsigned i = 0; i < osd_height; i += 8) {
        const std::array<std::uint8_t, 8> tmp = rotate_char(&title_[i]);
        for (unsigned k = 0; k < 8; ++k)
            title_[i + k] = tmp[k];
    }
}

void OsdRenderer::stage_row(
    OsdRow row,
    std::string_view s,
    bool inverted,
    bool stippled,
    std::uint8_t leftchar
) {

    if (row.v != 0 && (height_.v == 0 || row.v + 1u < height_.v))
        leftchar = 0;

    std::uint8_t stipple_mask = 0xFF;
    std::uint8_t stipple_tog = 0;
    if (stippled) {
        stipple_mask = 0x55;
        stipple_tog = 0xFF;
    }
    const std::uint8_t xormask = inverted ? 0xFF : 0x00;
    std::uint8_t xorchar = 0;
    const bool on_last_row = (row.v + 1u == height_.v);
    bool arrow_left = on_last_row && ((arrow_ & kArrowLeft) != 0);
    const bool arrow_right = on_last_row && ((arrow_ & kArrowRight) != 0);

    const unsigned linelimit = arrow_right ? kLineBytes - 22 : kLineBytes;

    unsigned i = 0;
    unsigned pos = 0;
    std::size_t si = 0;
    for (;;) {
        if (i == 0 && row.v < height_.v) {

            if (stripe_ == Stripe::Blank) {
                std::fill_n(scratch_ + pos, kSideStripeBytes, std::uint8_t{0});
                pos += kSideStripeBytes;
                i += kSideStripeBytes;
                continue;
            }
            const std::uint8_t* p;
            std::array<std::uint8_t, 8> rotated{};
            if (leftchar != 0) {
                rotated = rotate_char(kGlyphRom[leftchar]);
                p = rotated.data();
            } else {
                p = &title_[(height_.v - 1 - row.v) * 8];
            }
            scratch_[pos++] = 0xFF;
            scratch_[pos++] = 0xFF;
            scratch_[pos++] = 0xFF;
            for (unsigned k = 0; k < 8; ++k) {
                const auto v = static_cast<std::uint8_t>(255 ^ p[k]);
                scratch_[pos++] = v;
                scratch_[pos++] = v;
            }
            scratch_[pos++] = 0xFF;
            scratch_[pos++] = 0;
            scratch_[pos++] = 0;
            i += kSideStripeBytes;
            continue;
        }
        if (arrow_left && row.v + 1u == height_.v) {

            arrow_left = false;
            scratch_[pos++] = xormask;
            scratch_[pos++] = xormask;
            scratch_[pos++] = xormask;
            for (unsigned k = 0; k < 8; ++k)
                scratch_[pos++] = static_cast<std::uint8_t>(kGlyphRom[0x10][k] ^ xormask);
            for (unsigned k = 0; k < 8; ++k)
                scratch_[pos++] = static_cast<std::uint8_t>(kGlyphRom[0x14][k] ^ xormask);
            for (unsigned k = 0; k < 5; ++k)
                scratch_[pos++] = xormask;
            i += 24;
            bool truncated = false;
            for (unsigned k = 0; k < 3; ++k) {
                if (si >= s.size() || s[si] == '\0') {
                    truncated = true;
                    break;
                }
                ++si;
            }
            if (truncated)
                break;
            continue;
        }
        if (si >= s.size())
            break;
        const char ch = s[si++];
        if (ch == '\0')
            break;
        if (ch == kEscStipple) {
            stipple_mask ^= 0xAA;
            stipple_tog ^= 0xFF;
        } else if (ch == kEscXorChar) {
            xorchar ^= 0xFF;
        } else if (ch == '\n' || ch == '\r') {
            ++newline_refusals_;
            break;
        } else if (i < linelimit - 8) {

            const std::uint8_t* p = kGlyphRom[static_cast<unsigned char>(ch)];
            for (unsigned c = 0; c < 8; ++c) {
                scratch_[pos++] = static_cast<std::uint8_t>(
                    ((p[c] & stipple_mask) ^ xormask ^ xorchar) & 0xFF
                );
                stipple_mask ^= stipple_tog;
            }
            i += 8;
        }
    }
    for (; i < linelimit; ++i)
        scratch_[pos++] = xormask;

    if (arrow_right) {

        scratch_[pos++] = xormask;
        scratch_[pos++] = xormask;
        scratch_[pos++] = xormask;
        for (unsigned k = 0; k < 8; ++k)
            scratch_[pos++] = static_cast<std::uint8_t>(kGlyphRom[0x15][k] ^ xormask);
        for (unsigned k = 0; k < 8; ++k)
            scratch_[pos++] = static_cast<std::uint8_t>(kGlyphRom[0x11][k] ^ xormask);
        scratch_[pos++] = xormask;
        scratch_[pos++] = xormask;
        scratch_[pos++] = xormask;
    }
}

void OsdRenderer::stage_from_surface(OsdRow row) {
    const auto cur = static_cast<const proto::OsdSurface&>(*surface_).row(row);
    std::memcpy(scratch_, cur.data(), proto::OsdSurface::kRowBytes);
}

void OsdRenderer::commit(OsdRow row) {
    if (row.v >= proto::OsdSurface::kMaxRows)
        return;
    const auto dst = surface_->row(row);
    if (std::memcmp(dst.data(), scratch_, proto::OsdSurface::kRowBytes) == 0) {
        ++suppressed_;
        return;
    }
    std::memcpy(dst.data(), scratch_, proto::OsdSurface::kRowBytes);
    surface_->publish(row);
    ++publishes_;
}

void OsdRenderer::clear() {
    surface_->clear();
}

void OsdRenderer::text(
    OsdRow row,
    std::string_view s,
    bool inverted,
    bool stippled,
    std::uint8_t leftchar
) {
    stage_row(row, s, inverted, stippled, leftchar);
    commit(row);
}

void OsdRenderer::key_value(OsdRow row, std::string_view name, std::string_view value) {
    char buf[kOptionRowWidth + 1] = {};
    const std::size_t n = format_key_value(std::span<char>(buf, sizeof buf), name, value);
    text(row, std::string_view(buf, n), false);
}

void OsdRenderer::glyph(OsdRow row, unsigned col, Glyph g) {
    const unsigned first = (row.v < height_.v) ? kSideStripeBytes : 0;
    const unsigned at = first + col * 8;
    if (at + 8 > kLineBytes)
        return;
    stage_from_surface(row);
    const std::uint8_t* p = kGlyphRom[static_cast<unsigned>(g)];
    for (unsigned c = 0; c < 8; ++c)
        scratch_[at + c] = p[c];
    commit(row);
}

void OsdRenderer::box(OsdRow top, OsdRow bottom) {
    if (bottom.v < top.v)
        return;
    char rule[32];
    std::fill(std::begin(rule), std::end(rule), static_cast<char>(0x81));
    for (unsigned r = top.v; r <= bottom.v && r < proto::OsdSurface::kMaxRows; ++r) {
        const OsdRow row{static_cast<std::uint8_t>(r)};
        const bool edge = (r == top.v || r == bottom.v);
        const unsigned cells = (r < height_.v) ? 29u : 31u;
        stage_row(row, edge ? std::string_view(rule, cells) : std::string_view{}, false, false, 0);
        commit(row);
    }
}

void OsdRenderer::bar(
    OsdRow row,
    unsigned col,
    std::uint32_t value,
    std::uint32_t max,
    unsigned width
) {
    if (width == 0)
        return;
    const unsigned first = (row.v < height_.v) ? kSideStripeBytes : 0;
    const unsigned at = first + col * 8;
    if (at + width * 8 > kLineBytes)
        return;
    const std::uint32_t filled = (max == 0) ? 0 : ((value >= max) ? width : (value * width) / max);
    stage_from_surface(row);
    for (unsigned k = 0; k < width; ++k) {
        const auto g = static_cast<unsigned>(k < filled ? Glyph::BarFilled : Glyph::BarEmpty);
        const std::uint8_t* p = kGlyphRom[g];
        for (unsigned c = 0; c < 8; ++c)
            scratch_[at + k * 8 + c] = p[c];
    }
    commit(row);
}

void OsdRenderer::progress(OsdRow row, unsigned step) {
    static constexpr char kPartial[] = {'\x8C', '\x8E', '\x8F', '\x90', '\x91', '\x7F'};
    constexpr unsigned kSteps = sizeof kPartial;
    constexpr unsigned kLine = 28;
    if (step > kProgressMax)
        step = kProgressMax;
    char buf[kLine + 2];
    unsigned n = 0;
    buf[n++] = ' ';
    for (unsigned i = 0; i < step / kSteps; ++i)
        buf[n++] = static_cast<char>(Glyph::BarFilled);
    buf[n++] = kPartial[step % kSteps];
    text(row, std::string_view(buf, n < kLine ? n : kLine));
}

void OsdRenderer::scroll_stripes(bool more_above, bool more_below) {
    if (height_.v == 0)
        return;
    if (more_above)
        glyph(OsdRow{0}, 0, Glyph::ArrowUp);
    if (more_below) {
        glyph(OsdRow{static_cast<std::uint8_t>(height_.v - 1)}, 0, Glyph::ArrowDown);
    }
}

void OsdRenderer::composite_status_bar(const StatusBarFacts& facts) {
    text(OsdRow{16}, std::string_view{}, true);

    char str[40] = {};
    std::size_t len = 0;
    if (facts.splash) {
        static constexpr char kSplash[] = "       www.MiSTerFPGA.org       ";
        len = sizeof(kSplash) - 1;
        std::memcpy(str, kSplash, len);
    } else {
        static constexpr char kPrefix[] = " MiSTer      ";
        len = sizeof(kPrefix) - 1;
        std::memcpy(str, kPrefix, len);
        for (std::size_t i = 0; i < sizeof facts.clock && facts.clock[i] != '\0'; ++i) {
            if (len + 1 >= sizeof str)
                break;
            str[len++] = facts.clock[i];
        }

        std::size_t n = 8;

        if (facts.net_wifi)
            str[n++] = static_cast<char>(0x1D);
        if (facts.net_wired)
            str[n++] = static_cast<char>(0x1C);
        if (facts.bluetooth)
            str[n++] = static_cast<char>(0x04);
        if ((facts.sdram_cfg & 0x8000u) != 0) {
            switch (facts.sdram_cfg & 7u) {
                case 7:
                    str[n] = static_cast<char>(0x95);
                    break;
                case 3:
                    str[n] = static_cast<char>(0x94);
                    break;
                case 1:
                    str[n] = static_cast<char>(0x93);
                    break;
                default:
                    str[n] = static_cast<char>(0x92);
                    break;
            }
        }
        if (len > 22)
            str[22] = ' ';
    }
    text(OsdRow{17}, std::string_view(str, len), true);
    text(OsdRow{18}, std::string_view{}, true);
}

}
