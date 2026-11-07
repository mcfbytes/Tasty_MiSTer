// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/cdi_block.h"

#include <utility>

namespace mister::cores::cdi {
namespace {

constexpr void put_rw_word(
    std::span<std::uint8_t, sizeof(std::uint16_t)> word,
    std::uint8_t symbol
) noexcept {
    word[0] = 0;
    word[1] = symbol;
}

}

QFrame compose_q(std::int32_t lba, const svc::Toc& toc) noexcept {

    const svc::Msf abs = svc::Msf::of_wrapped(lba);

    const HeadLba head{lba};
    svc::TrackIndex track{};

    while (toc.last.contains(track) && head >= HeadLba::of(toc.at(track).end))
        track = track + svc::TrackCount{1};
    SectorCount into{};
    if (toc.last.contains(track))
        into = head - HeadLba::of(toc.at(track).start);
    unsigned index = 1;
    if (into < SectorCount{}) {
        into = into.magnitude();
        index = 0;
    }
    const svc::Msf rel = into.wrapped_msf();

    QFrame q{};
    if (toc.last.contains(track)) {
        const svc::TrackType type = toc.at(track).type;
        q.set(QFrame::Field::Control, type != svc::TrackType::Cdda ? 0x41u : 0x01u);
    }
    q.set(QFrame::Field::Point, bcd(track.number()));
    q.set(QFrame::Field::Index, bcd(index));
    q.set(QFrame::Field::Minute, rel.m);
    q.set(QFrame::Field::Second, rel.s);
    q.set(QFrame::Field::Frame, rel.f);
    q.set(QFrame::Field::Zero, 0);
    q.set(QFrame::Field::AMinute, abs.m);
    q.set(QFrame::Field::ASecond, abs.s);
    q.set(QFrame::Field::AFrame, abs.f);
    q.set(QFrame::Field::CrcHigh, 0xFF);
    q.set(QFrame::Field::CrcLow, 0xFF);
    q.seal();
    return q;
}

QFrame compose_toc_q(std::int32_t lba, std::span<const TocEntry> entries) noexcept {

    lba += 65536;
    QFrame q{};
    const svc::Msf abs = svc::Msf::of_wrapped(lba);
    if (entries.empty())
        return q;
    const auto n = static_cast<std::int32_t>(entries.size());
    std::int32_t at = lba % n;
    if (at < 0)
        at += n;
    const TocEntry& e = entries[static_cast<std::size_t>(at)];
    q.set(QFrame::Field::Control, e[0]);
    q.set(QFrame::Field::Point, 0);
    q.set(QFrame::Field::Index, e[1]);
    q.set(QFrame::Field::Minute, abs.m);
    q.set(QFrame::Field::Second, abs.s);
    q.set(QFrame::Field::Frame, abs.f);
    q.set(QFrame::Field::Zero, 0);
    q.set(QFrame::Field::AMinute, e[2]);
    q.set(QFrame::Field::ASecond, e[3]);
    q.set(QFrame::Field::AFrame, e[4]);
    q.set(QFrame::Field::CrcHigh, 0xFF);
    q.set(QFrame::Field::CrcLow, 0xFF);
    q.seal();
    return q;
}

void widen_rw(
    std::span<const std::uint8_t, kRawSubcodeBytes> symbols,
    std::span<std::uint8_t, kRwBytes> rw
) noexcept {

    for (std::size_t i = 0; i < kRawSubcodeBytes; ++i)
        put_rw_word(
            rw.subspan(i * sizeof(std::uint16_t)).first<sizeof(std::uint16_t)>(),
            symbols[i]
        );
}

std::size_t build_toc(
    const svc::Toc& toc,
    DiscType disc_type,
    std::span<TocEntry, kTocEntries> out
) noexcept {

    std::size_t n = 0;
    auto add = [&](std::uint8_t control,
                   std::uint8_t point,
                   std::uint8_t m,
                   std::uint8_t s,
                   std::uint8_t f) {
        for (int k = 0; k < 3 && n < out.size(); ++k)
            out[n++] = TocEntry{control, point, m, s, f};
    };
    auto msf = [](HeadLba at) {
        const svc::Msf packed = at.wrapped_msf();
        return TocEntry{packed.m, packed.s, packed.f, 0, 0};
    };
    toc.last.for_each_index([&](svc::TrackIndex i) {
        const svc::Track& row = toc.at(i);
        const TocEntry t = msf(HeadLba::of(row.start));
        add(row.type != svc::TrackType::Cdda ? std::uint8_t{0x41} : std::uint8_t{0x01},
            bcd(i.number()),
            t[0],
            t[1],
            t[2]);
    });
    add(1, 0xA0, 1, std::to_underlying(disc_type), 0);
    add(1, 0xA1, toc.last.bcd_byte(), 0, 0);
    const TocEntry lead_out = msf(HeadLba::of(toc.end));
    add(1, 0xA2, lead_out[0], lead_out[1], lead_out[2]);
    return n;
}

DiscType disc_type(const svc::Toc& toc, DiscType previous) noexcept {

    bool audio = false, mode1 = false, mode2 = false;
    toc.last.for_each_index([&](svc::TrackIndex i) {
        switch (toc.at(i).type) {
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
    });
    if (audio && !mode1 && !mode2)
        return DiscType::CdRom;
    if (mode1 && !mode2)
        return DiscType::CdRom;
    if (!mode1 && mode2)
        return DiscType::CdRomXa;
    return previous;
}

}
