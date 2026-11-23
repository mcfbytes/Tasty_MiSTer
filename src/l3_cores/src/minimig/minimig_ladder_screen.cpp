// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/minimig/minimig_ladder_screen.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

#include "cores/minimig/minimig_ladder_font.h"
#include "proto/types.h"

namespace mister::cores::minimig {

namespace {

constexpr BusBytes kPlane = BootScreen::kRow * 256u;
constexpr BusAddr kPlane1 = kBootScreen;
constexpr BusAddr kPlane2 = kPlane1 + kPlane;

constexpr std::size_t kLogoRows = 32;
constexpr std::size_t kLogoRowBytes = 208 / 8;
constexpr BusBytes kLogoTop = BootScreen::kRow * 64u + BusBytes{24};
constexpr BusAddr kBall{0x08A000};
constexpr BusAddr kCopper{0x08E680};

constexpr proto::FileSize kSector{512};

constexpr BusBytes kCleared = (kPlane + kPlane) * 2u;

static_assert(kPlane2 == BusAddr{0x085000} && kCleared == BusBytes{2 * 40960});
static_assert(proto::FileSize{2 * kLogoRows * kLogoRowBytes} == BootScreen::kLogoBytes);

struct Cursor {
    BusAddr at{};
    proto::FileOffset read{};
};

proto::FileOffset upload(FrameQueue& out, Cursor from, std::span<const std::byte> data) {
    while (!data.empty()) {
        const proto::FileSize into_sector = (from.read - proto::kFileStart)
                                                .bytes_past_whole(kSector);
        const std::span<const std::byte> piece = data.first(
            (kSector - into_sector).clamp_write(proto::kFileStart, data.size())
        );
        out.mem(from.at, piece);
        from.at += BusBytes::of(piece);
        from.read += proto::FileSize::of(piece);
        data = data.subspan(piece.size());
    }
    return from.read;
}

std::vector<std::byte> sized(std::span<const std::byte> file, proto::FileSize want) {
    auto out = want.zeroed<std::vector<std::byte>>();
    std::ranges::copy(file.first(std::min(file.size(), out.size())), out.begin());
    return out;
}

void upload_logo(FrameQueue& out, std::span<const std::byte> file) {
    const std::vector<std::byte> logo = sized(file, BootScreen::kLogoBytes);
    std::span<const std::byte> rest{logo};
    proto::FileOffset read{};
    for (const BusAddr plane : {kPlane1, kPlane2}) {
        BusAddr row = plane + kLogoTop;
        for (std::size_t n = 0; n < kLogoRows; ++n) {
            read = upload(out, {.at = row, .read = read}, rest.first(kLogoRowBytes));
            rest = rest.subspan(kLogoRowBytes);
            row += BootScreen::kRow;
        }

        out.mem(row, {});
    }
}

constexpr std::array<std::uint16_t, 12> kDefaultCopper{
    0x00E0,
    0x0008,
    0x00E2,
    0x0000,
    0x00E4,
    0x0008,
    0x00E6,
    0x5000,
    0x0100,
    0xA200,
    0xFFFF,
    0xFFFE
};

void custom_init(FrameQueue& out) {
    constexpr std::array<std::uint16_t, 1> kFmodeValue{0x0000};
    constexpr std::array<std::uint16_t, 1> kCopconValue{0x0002};
    constexpr std::array<std::uint16_t, 4> kCopperPointers{0x0008, 0xE680, 0x0008, 0xE69C};
    constexpr std::array<std::uint16_t, 8>
        kDisplay{0x2C81, 0xF4C1, 0x003C, 0x00D4, 0x87C0, 0x0000, 0x7FFF, 0x7FFF};
    constexpr std::array<std::uint16_t, 4> kPlanePointers{0x0008, 0x0000, 0x0008, 0x5000};
    constexpr std::array<std::uint16_t, 6>
        kPlaneControl{0xA200, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000};
    constexpr std::array<std::uint16_t, 4> kBlitter{0x09F0, 0x0000, 0xFFFF, 0xFFFF};
    constexpr std::array<std::uint16_t, 2> kBlitterModulo{0x0000, 0x0040};
    constexpr std::array<std::uint16_t, 4> kColours{0x0000, 0x0AAA, 0x0A00, 0x000A};
    constexpr std::array<std::uint16_t, 1> kCopperJump{0x0000};
    constexpr BusAddr kFmode = kCustomChips + BusBytes{0x1FC};
    constexpr BusAddr kCopcon = kCustomChips + BusBytes{0x02E};
    constexpr BusAddr kCop1lc = kCustomChips + BusBytes{0x080};
    constexpr BusAddr kDiwstrt = kCustomChips + BusBytes{0x08E};
    constexpr BusAddr kBpl1pt = kCustomChips + BusBytes{0x0E0};
    constexpr BusAddr kBplcon0 = kCustomChips + BusBytes{0x100};
    constexpr BusAddr kBltcon0 = kCustomChips + BusBytes{0x040};
    constexpr BusAddr kBltamod = kCustomChips + BusBytes{0x064};
    constexpr BusAddr kColor00 = kCustomChips + BusBytes{0x180};
    constexpr BusAddr kCopjmp1 = kCustomChips + BusBytes{0x088};
    out.mem_words(kFmode, kFmodeValue);
    out.mem_words(kCopcon, kCopconValue);
    out.mem_words(kCop1lc, kCopperPointers);
    out.mem_words(kDiwstrt, kDisplay);
    out.mem_words(kBpl1pt, kPlanePointers);
    out.mem_words(kBplcon0, kPlaneControl);
    out.mem_words(kBltcon0, kBlitter);
    out.mem_words(kBltamod, kBlitterModulo);
    out.mem_words(kColor00, kColours);
    out.mem_words(kCopjmp1, kCopperJump);
}

}

void BootScreen::draw(FrameQueue& out, const Assets& assets) {

    out.mem_zeros(kPlane1, kCleared);
    if (!assets.logo.empty())
        upload_logo(out, assets.logo);
    if (!assets.ball.empty())
        upload(out, {.at = kBall}, sized(assets.ball, kBallBytes));
    if (!assets.copper.empty()) {
        upload(out, {.at = kCopper}, sized(assets.copper, kCopperBytes));
    } else {
        out.mem_words(kCopper, kDefaultCopper);
    }
    custom_init(out);
}

void BootScreen::print(FrameQueue& out, std::string_view text) {
    text = text.substr(0, kColumns);
    std::vector<std::byte> line;
    for (std::size_t glyph_line = 0; glyph_line < BootFont::kLines; ++glyph_line) {
        line.clear();
        for (const char ch : text)
            line.push_back(std::byte{BootFont::glyph(ch)[glyph_line]});

        if (line.size() % sizeof(std::uint16_t) != 0)
            line.push_back(std::byte{BootFont::glyph(BootFont::kFirst)[glyph_line]});
        out.mem(cursor_, line);
        cursor_ += kRow;
    }
}

}
