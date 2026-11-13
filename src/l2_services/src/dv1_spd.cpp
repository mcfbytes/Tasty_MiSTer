// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/dv1_spd.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "infra/endian.h"
#include "svc/adv7513_spd_packet.h"
#include "svc/video_sample.h"

namespace mister::svc {
namespace {

constexpr std::array<std::uint8_t, 7> kDv1Header{0x83, 0x01, 25, 0x00, 'D', 'V', '1'};

enum class Dv1At : std::uint8_t {
    kFlags = 7,
    kPixRep = 8,
    kDeH = 9,
    kDeV = 11,
    kWidth = 13,
    kHeight = 15,
    kName = 17,
};

enum class Dv1Flag : std::uint8_t {
    kInterlaced = 1,
    kMenu = 4,
    kRotated = 8,
    kDirNone = 0x00,
    kDirCw = 0x10,
    kDirCcw = 0x20,
};

constexpr std::uint8_t at(Dv1At a) noexcept {
    return static_cast<std::uint8_t>(a);
}

constexpr std::uint8_t mask(Dv1Flag f) noexcept {
    return static_cast<std::uint8_t>(f);
}

constexpr std::uint8_t or_flag(std::uint8_t bits, Dv1Flag f) noexcept {
    return static_cast<std::uint8_t>(bits | mask(f));
}

struct SpdByte {
    std::uint8_t v = 0;

    [[nodiscard]] static constexpr SpdByte of(std::uint32_t field) noexcept {
        return SpdByte{static_cast<std::uint8_t>(field)};
    }

    constexpr void store(std::span<std::uint8_t> payload, std::size_t index) const noexcept {
        payload[index] = v;
    }
};

struct SpdWord {
    std::uint16_t v = 0;

    [[nodiscard]] static constexpr SpdWord of(std::uint32_t field) noexcept {
        return SpdWord{static_cast<std::uint16_t>(field)};
    }

    constexpr void store(std::span<std::uint8_t> payload, std::size_t index) const noexcept {
        infra::store_le<std::uint16_t>(payload.subspan(index).first<sizeof(std::uint16_t)>(), v);
    }
};

}

adv7513::SpdPacket build_dv1_spd(const Dv1Facts& f, std::string_view name) noexcept {
    adv7513::SpdPacket p{};
    std::copy(kDv1Header.begin(), kDv1Header.end(), p.b.begin());

    std::uint8_t flags = mask(Dv1Flag::kDirNone);
    switch (f.dir) {
        case proto::RotationDir::Cw:
            flags = mask(Dv1Flag::kDirCw);
            break;
        case proto::RotationDir::Ccw:
            flags = mask(Dv1Flag::kDirCcw);
            break;
        case proto::RotationDir::None:
            break;
    }
    if (f.sample.interlaced())
        flags = or_flag(flags, Dv1Flag::kInterlaced);
    if (f.menu)
        flags = or_flag(flags, Dv1Flag::kMenu);
    if (f.sample.rotated())
        flags = or_flag(flags, Dv1Flag::kRotated);
    p.b[at(Dv1At::kFlags)] = flags;

    const std::uint32_t width = f.sample.active.width;
    const std::uint32_t pixrep = f.sample.pixrep ? f.sample.pixrep
                                                 : (width != 0 ? f.sample.ctime / width : 0);
    SpdByte::of(pixrep).store(std::span(p.b), at(Dv1At::kPixRep));
    SpdWord::of(f.sample.de_h).store(std::span(p.b), at(Dv1At::kDeH));
    SpdWord::of(f.sample.de_v).store(std::span(p.b), at(Dv1At::kDeV));
    SpdWord::of(width).store(std::span(p.b), at(Dv1At::kWidth));
    SpdWord::of(f.sample.active.height).store(std::span(p.b), at(Dv1At::kHeight));

    std::size_t i = at(Dv1At::kName);
    for (const char c : name) {
        if (i == p.b.size())
            break;
        p.b[i] = static_cast<std::uint8_t>(c);
        ++i;
    }
    return p;
}

}
