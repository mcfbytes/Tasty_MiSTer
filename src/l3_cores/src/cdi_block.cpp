// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/cdi_block.h"

#include <cstring>

namespace mister::cores::cdi {
namespace {

constexpr int kFramesPerMinute = 60 * 75;

void put_word(std::span<std::uint8_t> words, std::size_t at, unsigned v) noexcept {
    words[at] = static_cast<std::uint8_t>((v >> 8) & 0xFFu);
    words[at + 1] = static_cast<std::uint8_t>(v & 0xFFu);
}

void seal_crc(std::span<std::uint8_t, kQBytes> q) noexcept {
    std::uint16_t accum = 0;
    for (std::size_t i = 0; i < 12; ++i) {
        const unsigned data = q[1 + i * 2];
        accum = static_cast<std::uint16_t>(
            ((static_cast<unsigned>(accum) << 8) | data) ^ kCrcTable[accum >> 8]
        );
    }
    put_word(q, 20, (accum >> 8) & 0xFFu);
    put_word(q, 22, accum & 0xFFu);
}

}

void compose_q(std::int32_t lba, const svc::Toc& toc, std::span<std::uint8_t, kQBytes> q) noexcept {

    const auto am = static_cast<std::uint8_t>(lba / kFramesPerMinute);
    const int rem = lba - am * kFramesPerMinute;
    const auto as = static_cast<std::uint8_t>(rem / 75);
    const auto af = static_cast<std::uint8_t>(rem % 75);

    int track = 0;

    while (track < toc.last && lba > static_cast<std::int32_t>(toc.tracks[track].end.v) - 1)
        ++track;
    int track_lba = 0;
    if (track < toc.last)
        track_lba = lba - static_cast<std::int32_t>(toc.tracks[track].start.v);
    unsigned index = 1;
    if (track_lba < 0) {
        track_lba = -track_lba;
        index = 0;
    }
    const auto tm = static_cast<std::uint8_t>(track_lba / kFramesPerMinute);
    track_lba -= tm * kFramesPerMinute;
    const auto ts = static_cast<std::uint8_t>(track_lba / 75);
    const auto tf = static_cast<std::uint8_t>(track_lba % 75);

    if (track < toc.last) {
        put_word(q, 0, toc.tracks[track].type != svc::TrackType::Cdda ? 0x41u : 0x01u);
    }
    put_word(q, 2, bcd(static_cast<unsigned>(track + 1)));
    put_word(q, 4, bcd(index));
    put_word(q, 6, bcd(tm));
    put_word(q, 8, bcd(ts));
    put_word(q, 10, bcd(tf));
    put_word(q, 12, 0);
    put_word(q, 14, bcd(am));
    put_word(q, 16, bcd(as));
    put_word(q, 18, bcd(af));
    put_word(q, 20, 0xFF);
    put_word(q, 22, 0xFF);
    seal_crc(q);
}

void compose_toc_q(
    std::int32_t lba,
    std::span<const TocEntry> entries,
    std::span<std::uint8_t, kQBytes> q
) noexcept {

    lba += 65536;
    const auto am = static_cast<std::uint8_t>(lba / kFramesPerMinute);
    const int rem = lba - am * kFramesPerMinute;
    const auto as = static_cast<std::uint8_t>(rem / 75);
    const auto af = static_cast<std::uint8_t>(rem % 75);
    if (entries.empty())
        return;
    const auto n = static_cast<std::int32_t>(entries.size());
    std::int32_t at = lba % n;
    if (at < 0)
        at += n;
    const TocEntry& e = entries[static_cast<std::size_t>(at)];
    put_word(q, 0, e[0]);
    put_word(q, 2, 0);
    put_word(q, 4, e[1]);
    put_word(q, 6, bcd(am));
    put_word(q, 8, bcd(as));
    put_word(q, 10, bcd(af));
    put_word(q, 12, 0);
    put_word(q, 14, e[2]);
    put_word(q, 16, e[3]);
    put_word(q, 18, e[4]);
    put_word(q, 20, 0xFF);
    put_word(q, 22, 0xFF);
    seal_crc(q);
}

void widen_rw(
    std::span<const std::uint8_t, kRawSubcodeBytes> symbols,
    std::span<std::uint8_t, kRwBytes> rw
) noexcept {

    for (std::size_t i = 0; i < kRawSubcodeBytes; ++i)
        put_word(rw, i * 2, symbols[i]);
}

std::size_t build_toc(
    const svc::Toc& toc,
    std::uint8_t disc_type,
    std::span<TocEntry, kTocEntries> out
) noexcept {

    std::size_t n = 0;
    auto add = [&](unsigned control, unsigned point, unsigned m, unsigned s, unsigned f) {
        for (int k = 0; k < 3 && n < out.size(); ++k) {
            out[n++] = TocEntry{
                static_cast<std::uint8_t>(control),
                static_cast<std::uint8_t>(point),
                static_cast<std::uint8_t>(m),
                static_cast<std::uint8_t>(s),
                static_cast<std::uint8_t>(f)
            };
        }
    };

    struct Msf {
        unsigned m, s, f;
    };

    auto msf = [](int lba) {
        const auto mm = static_cast<std::uint8_t>(lba / kFramesPerMinute);
        lba -= mm * kFramesPerMinute;
        return Msf{
            bcd(mm),
            bcd(static_cast<std::uint8_t>(lba / 75)),
            bcd(static_cast<std::uint8_t>(lba % 75))
        };
    };
    for (int i = 0; i < toc.last; ++i) {
        const Msf t = msf(static_cast<int>(toc.tracks[i].start.v));
        add(toc.tracks[i].type != svc::TrackType::Cdda ? 0x41u : 0x01u,
            bcd(static_cast<unsigned>(i + 1)),
            t.m,
            t.s,
            t.f);
    }
    add(1, 0xA0, 1, disc_type, 0);
    add(1, 0xA1, bcd(toc.last), 0, 0);
    const Msf lead_out = msf(static_cast<int>(toc.end.v));
    add(1, 0xA2, lead_out.m, lead_out.s, lead_out.f);
    return n;
}

std::uint8_t disc_type(const svc::Toc& toc, std::uint8_t previous) noexcept {

    bool audio = false, mode1 = false, mode2 = false;
    for (int i = 0; i < toc.last; ++i) {
        switch (toc.tracks[i].type) {
            case svc::TrackType::Cdda:
                audio = true;
                break;
            case svc::TrackType::Mode1:
                mode1 = true;
                break;
            case svc::TrackType::Mode2:
                mode2 = true;
                break;
        }
    }
    if (audio && !mode1 && !mode2)
        return 0x00;
    if (mode1 && !mode2)
        return 0x00;
    if (!mode1 && mode2)
        return 0x20;
    return previous;
}

}
