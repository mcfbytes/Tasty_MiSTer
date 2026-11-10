// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/osd_renderer.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstring>

#include "app/link_tx_channel.h"
#include "ui/glyph_rom.h"
#include "ui/status_bar_facts.h"

namespace mister::ui {

FixedStr<kClassicRowColumns + 1, StrFit::Clip> format_key_value(
    std::string_view name,
    std::string_view value
) noexcept {
    constexpr std::size_t w = kClassicRowColumns;
    std::array<char, w> row{};
    std::fill(row.begin(), row.end(), ' ');

    std::size_t n = std::min(name.size(), w - 1);
    while (n > 1 && name[n - 1] == ' ')
        --n;
    for (std::size_t i = 0; i < n; ++i)
        row[i + 1] = name[i];
    std::size_t len = n + 1;

    const std::size_t vlen = std::min(value.size(), w);

    if (len + vlen > w - 1) {
        len = (vlen < w - 1) ? (w - 1 - vlen) : 1;
    }
    row[len] = ':';
    ++len;

    const std::size_t pad_to = (w > vlen) ? (w - vlen) : 0;
    for (std::size_t i = len; i < pad_to; ++i)
        row[i] = ' ';
    for (std::size_t i = 0; i < vlen; ++i)
        row[pad_to + i] = value[i];

    FixedStr<kClassicRowColumns + 1, StrFit::Clip> out;
    (void)out.assign(std::string_view{row.data(), row.size()});
    return out;
}

namespace {

constexpr unsigned kLineBytes = proto::OsdSurface::kRowBytes;

constexpr char kEscStipple = 0x0B;
constexpr char kEscXorChar = 0x0C;

[[nodiscard]] std::array<std::uint8_t, kGlyphBytes> rotate_char(const std::uint8_t* in) noexcept {
    std::array<std::uint8_t, kGlyphBytes> out{};
    for (std::size_t b = 0; b < kGlyphBytes; ++b) {
        unsigned a = 0;
        for (std::size_t c = 0; c < kGlyphBytes; ++c) {
            a <<= 1;
            const unsigned column = in[c];
            a |= (column >> b) & 1U;
        }
        out[b] = static_cast<std::uint8_t>(a);
    }
    return out;
}

}

void OsdRenderer::set_title(std::string_view s, Arrow arrow) noexcept {
    constexpr std::size_t kMinTitleBytes = 16;
    constexpr std::size_t kShortSpaceRun = 5;
    constexpr std::size_t kCentred = 2;
    arrow_ = arrow;

    std::size_t osd_height = height_.size() * kGlyphBytes;
    if (osd_height < kMinTitleBytes)
        osd_height = kMinTitleBytes;
    if (osd_height > sizeof title_)
        osd_height = sizeof title_;
    std::fill(std::begin(title_), std::end(title_), std::uint8_t{0});
    std::size_t zeros = 0;
    std::size_t outp = 0;
    for (const char ch : s) {
        const auto c = std::bit_cast<unsigned char>(ch);
        if (c == 0 || outp >= osd_height - kGlyphBytes)
            break;
        const std::uint8_t* p = kGlyphRom[c];
        for (std::size_t j = 0; j < kGlyphBytes; ++j) {
            const std::uint8_t nc = p[j];
            if (nc != 0) {
                zeros = 0;
                title_[outp++] = nc;
            } else if (zeros == 0 || (c == ' ' && zeros < kShortSpaceRun)) {
                title_[outp++] = 0;
                ++zeros;
            }
            if (outp >= sizeof title_)
                break;
        }
    }
    for (std::size_t i = outp; i < osd_height; ++i)
        title_[i] = 0;

    const std::size_t c = (outp + 1 < osd_height) ? ((osd_height - 1 - outp) / kCentred) : 0u;
    std::memmove(title_ + c, title_, outp);
    for (std::size_t i = 0; i < c; ++i)
        title_[i] = 0;

    for (std::size_t i = 0; i < osd_height; i += kGlyphBytes) {
        const auto tmp = rotate_char(&title_[i]);
        for (std::size_t k = 0; k < kGlyphBytes; ++k)
            title_[i + k] = tmp[k];
    }
}

void OsdRenderer::stage_row(
    OsdRow row,
    std::string_view s,
    bool inverted,
    bool stippled,
    std::optional<Glyph> leftchar
) {
    constexpr std::uint8_t kLit = 0xFF;
    constexpr std::uint8_t kStippleOdd = 0x55;
    constexpr std::uint8_t kStippleFlip = 0xAA;
    constexpr std::size_t kBorder = 3;
    constexpr std::size_t kDoubled = 2;
    constexpr std::size_t kGap = 2;
    constexpr std::size_t kLeftArrowBytes = 24;
    constexpr std::size_t kLeftArrowTail = 5;
    constexpr std::size_t kRightArrowBytes = 22;
    constexpr std::size_t kArrowSkippedChars = 3;
    constexpr std::array<unsigned char, 2> kLeftArrow{0x10, 0x14};
    constexpr std::array<unsigned char, 2> kRightArrow{0x15, 0x11};

    const RowCount above = row_index_of(row).before();
    const bool inside = in_height(row);
    const bool on_last_row = above + kOneRow == height_;

    if (row != OsdRow{} && (height_.empty() || above + kOneRow < height_))
        leftchar.reset();

    std::uint8_t stipple_mask = kLit;
    std::uint8_t stipple_tog = 0;
    if (stippled) {
        stipple_mask = kStippleOdd;
        stipple_tog = kLit;
    }
    const std::uint8_t xormask = inverted ? kLit : 0;
    std::uint8_t xorchar = 0;
    bool arrow_left = on_last_row && ((arrow_ & kArrowLeft) != 0);
    const bool arrow_right = on_last_row && ((arrow_ & kArrowRight) != 0);

    const std::size_t linelimit = arrow_right ? kLineBytes - kRightArrowBytes : kLineBytes;

    std::size_t i = 0;
    std::uint8_t* cur = scratch_;
    std::size_t si = 0;
    for (;;) {
        if (i == 0 && inside) {

            if (stripe_ == Stripe::Blank) {
                cur = std::fill_n(cur, kSideStripeBytes, std::uint8_t{0});
                i += kSideStripeBytes;
                continue;
            }
            const std::uint8_t* p;
            std::array<std::uint8_t, kGlyphBytes> rotated{};
            if (leftchar.has_value()) {
                rotated = rotate_char(kGlyphRom[glyph_code(*leftchar)]);
                p = rotated.data();
            } else {
                p = &title_[(height_ - kOneRow - above).size() * kGlyphBytes];
            }
            cur = std::fill_n(cur, kBorder, kLit);
            for (std::size_t k = 0; k < kGlyphBytes; ++k) {
                const std::uint8_t v = kLit ^ p[k];
                cur = std::fill_n(cur, kDoubled, v);
            }
            *cur++ = kLit;
            cur = std::fill_n(cur, kGap, std::uint8_t{0});
            i += kSideStripeBytes;
            continue;
        }
        if (arrow_left && on_last_row) {

            arrow_left = false;
            cur = std::fill_n(cur, kBorder, xormask);
            for (const unsigned char code : kLeftArrow) {
                for (std::size_t k = 0; k < kGlyphBytes; ++k)
                    *cur++ = kGlyphRom[code][k] ^ xormask;
            }
            cur = std::fill_n(cur, kLeftArrowTail, xormask);
            i += kLeftArrowBytes;
            bool truncated = false;
            for (std::size_t k = 0; k < kArrowSkippedChars; ++k) {
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
            stipple_mask ^= kStippleFlip;
            stipple_tog ^= kLit;
        } else if (ch == kEscXorChar) {
            xorchar ^= kLit;
        } else if (ch == '\n' || ch == '\r') {
            ++newline_refusals_;
            break;
        } else if (i < linelimit - kGlyphBytes) {

            const std::uint8_t* p = kGlyphRom[std::bit_cast<unsigned char>(ch)];
            for (std::size_t c = 0; c < kGlyphBytes; ++c) {
                *cur++ = (p[c] & stipple_mask) ^ xormask ^ xorchar;
                stipple_mask ^= stipple_tog;
            }
            i += kGlyphBytes;
        }
    }
    for (; i < linelimit; ++i)
        *cur++ = xormask;

    if (arrow_right) {

        cur = std::fill_n(cur, kBorder, xormask);
        for (const unsigned char code : kRightArrow) {
            for (std::size_t k = 0; k < kGlyphBytes; ++k)
                *cur++ = kGlyphRom[code][k] ^ xormask;
        }
        cur = std::fill_n(cur, kBorder, xormask);
    }
}

void OsdRenderer::stage_from_surface(OsdRow row) {
    const auto cur = static_cast<const proto::OsdSurface&>(surface_).row(row);
    std::memcpy(scratch_, cur.data(), proto::OsdSurface::kRowBytes);
}

void OsdRenderer::commit(OsdRow row) {
    if (!proto::OsdSurface::kMaxRows.covers(row))
        return;
    const auto dst = surface_.row(row);
    if (std::memcmp(dst.data(), scratch_, proto::OsdSurface::kRowBytes) == 0) {
        ++suppressed_;
        return;
    }
    std::memcpy(dst.data(), scratch_, proto::OsdSurface::kRowBytes);
    surface_.publish(row);
    ++publishes_;
}

void OsdRenderer::clear() {
    surface_.clear();
}

void OsdRenderer::text(
    OsdRow row,
    std::string_view s,
    bool inverted,
    bool stippled,
    std::optional<Glyph> leftchar
) {
    stage_row(row, s, inverted, stippled, leftchar);
    commit(row);
}

void OsdRenderer::key_value(OsdRow row, std::string_view name, std::string_view value) {
    text(row, format_key_value(name, value).view(), false);
}

void OsdRenderer::glyph(OsdRow row, unsigned col, Glyph g) {
    const std::size_t first = in_height(row) ? kSideStripeBytes : 0;
    const std::size_t at = first + col * kGlyphBytes;
    if (at + kGlyphBytes > kLineBytes)
        return;
    stage_from_surface(row);
    const std::uint8_t* p = kGlyphRom[glyph_code(g)];
    for (std::size_t c = 0; c < kGlyphBytes; ++c)
        scratch_[at + c] = p[c];
    commit(row);
}

void OsdRenderer::box(OsdRow top, OsdRow bottom) {
    constexpr char kRuleCell = '\x81';
    constexpr std::size_t kCellsBesideStripe = 29;
    constexpr std::size_t kCellsFullRow = 31;
    if (bottom < top)
        return;
    char rule[kOsdColumns];
    std::fill(std::begin(rule), std::end(rule), kRuleCell);
    for (OsdRow row = top; row <= bottom && proto::OsdSurface::kMaxRows.covers(row);
         row += proto::kOneOsdRow) {
        const bool edge = (row == top || row == bottom);
        const std::size_t cells = in_height(row) ? kCellsBesideStripe : kCellsFullRow;
        stage_row(
            row,
            edge ? std::string_view(rule, cells) : std::string_view{},
            false,
            false,
            std::nullopt
        );
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
    const std::size_t first = in_height(row) ? kSideStripeBytes : 0;
    const std::size_t at = first + col * kGlyphBytes;
    if (at + width * kGlyphBytes > kLineBytes)
        return;
    const std::uint32_t filled = (max == 0) ? 0 : ((value >= max) ? width : (value * width) / max);
    stage_from_surface(row);
    for (unsigned k = 0; k < width; ++k) {
        const std::uint8_t*
            p = kGlyphRom[glyph_code(k < filled ? Glyph::BarFilled : Glyph::BarEmpty)];
        for (std::size_t c = 0; c < kGlyphBytes; ++c)
            scratch_[at + k * kGlyphBytes + c] = p[c];
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
        buf[n++] = glyph_char(Glyph::BarFilled);
    buf[n++] = kPartial[step % kSteps];
    text(row, std::string_view(buf, n < kLine ? n : kLine));
}

void OsdRenderer::scroll_stripes(bool more_above, bool more_below) {
    if (height_.empty())
        return;
    if (more_above)
        glyph(OsdRow{}, 0, Glyph::ArrowUp);
    if (more_below) {
        glyph(last_osd_row(height_), 0, Glyph::ArrowDown);
    }
}

void OsdRenderer::composite_status_bar(const StatusBarFacts& facts) {
    constexpr std::size_t kIconColumn = 8;
    constexpr std::size_t kWeekdayEnd = 22;
    constexpr char kGlyphWifi = '\x1D';
    constexpr char kGlyphWired = '\x1C';
    constexpr char kGlyphBluetooth = '\x04';
    constexpr char kGlyphSdram128M = '\x95';
    constexpr char kGlyphSdram64M = '\x94';
    constexpr char kGlyphSdram32M = '\x93';
    constexpr char kGlyphSdramOther = '\x92';
    const OsdRow clock_row = proto::OsdSurface::kStatusRow + proto::kOneOsdRow;

    text(proto::OsdSurface::kStatusRow, std::string_view{}, true);

    char str[kStatusBarBytes] = {};
    std::size_t len = 0;
    if (facts.splash) {
        static constexpr char kSplash[] = "       www.MiSTerFPGA.org       ";
        len = sizeof(kSplash) - 1;
        std::memcpy(str, kSplash, len);
    } else {
        static constexpr char kPrefix[] = " MiSTer      ";
        len = sizeof(kPrefix) - 1;
        std::memcpy(str, kPrefix, len);
        const std::string_view clock = facts.clock.view();
        const std::size_t clock_room = sizeof str - 1 - len;
        const std::size_t clock_n = std::min(clock.size(), clock_room);
        std::copy_n(clock.data(), clock_n, str + len);
        len += clock_n;

        std::size_t n = kIconColumn;

        if (facts.net_wifi)
            str[n++] = kGlyphWifi;
        if (facts.net_wired)
            str[n++] = kGlyphWired;
        if (facts.bluetooth)
            str[n++] = kGlyphBluetooth;
        if (facts.sdram_cfg.has(proto::kSdramPresent)) {
            const proto::OsdMask size = facts.sdram_cfg.only(proto::kSdramSizeBits);
            if (size == proto::kSdramSize128) {
                str[n] = kGlyphSdram128M;
            } else if (size == proto::kSdramSize64) {
                str[n] = kGlyphSdram64M;
            } else if (size == proto::kSdramSize32) {
                str[n] = kGlyphSdram32M;
            } else {
                str[n] = kGlyphSdramOther;
            }
        }
        if (len > kWeekdayEnd)
            str[kWeekdayEnd] = ' ';
    }
    text(clock_row, std::string_view(str, len), true);
    text(clock_row + proto::kOneOsdRow, std::string_view{}, true);
}

}
