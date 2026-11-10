// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/cdi_block.h"

#include <utility>

#include "cores/toc_entry.h"

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

QFrame compose_q(HeadLba lba, const svc::Toc& toc) noexcept {

    const svc::Msf abs = lba.wrapped_msf();

    svc::TrackIndex track{};

    while (toc.last.contains(track) && lba >= HeadLba::of(toc.at(track).end))
        track = track + svc::TrackCount{1};
    SectorCount into{};
    if (toc.last.contains(track))
        into = lba - HeadLba::of(toc.at(track).start);
    unsigned index = 1;
    if (into < SectorCount{}) {
        into = into.magnitude();
        index = 0;
    }
    const svc::Msf rel = into.wrapped_msf();

    QControl control{};
    if (toc.last.contains(track)) {
        const svc::TrackType type = toc.at(track).type;
        control = QControl::toc(type != svc::TrackType::Cdda);
    }
    QFrame q{control};
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

QFrame compose_toc_q(HeadLba lba, std::span<const TocEntry> entries) noexcept {

    const HeadLba phase = lba + SectorCount{65536};
    const svc::Msf abs = phase.wrapped_msf();
    if (entries.empty())
        return QFrame{};
    const TocEntry& e = phase.row_in(entries);
    QFrame q{e.control, e.point};
    q.set(QFrame::Field::Point, 0);
    q.set(QFrame::Field::Minute, abs.m);
    q.set(QFrame::Field::Second, abs.s);
    q.set(QFrame::Field::Frame, abs.f);
    q.set(QFrame::Field::Zero, 0);
    q.set(QFrame::Field::AMinute, e.at.m);
    q.set(QFrame::Field::ASecond, e.at.s);
    q.set(QFrame::Field::AFrame, e.at.f);
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

std::span<const TocEntry> build_toc(
    const svc::Toc& toc,
    DiscType disc_type,
    std::span<TocEntry, kTocEntries> out
) noexcept {

    std::size_t n = 0;
    auto add = [&](QControl control, TocPoint point, svc::Msf at) {
        for (int k = 0; k < 3 && n < out.size(); ++k)
            out[n++] = TocEntry{control, point, at};
    };
    toc.last.for_each_index([&](svc::TrackIndex i) {
        const svc::Track& row = toc.at(i);
        add(QControl::toc(row.type != svc::TrackType::Cdda),
            TocPoint::of_track(i.number()),
            HeadLba::of(row.start).wrapped_msf());
    });
    add(QControl::toc(false),
        TocPoint::first_track_info(),
        svc::Msf{1, std::to_underlying(disc_type), 0});
    add(QControl::toc(false), TocPoint::last_track_info(), svc::Msf{toc.last.bcd_byte(), 0, 0});
    add(QControl::toc(false), TocPoint::lead_out(), HeadLba::of(toc.end).wrapped_msf());
    return out.first(n);
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
