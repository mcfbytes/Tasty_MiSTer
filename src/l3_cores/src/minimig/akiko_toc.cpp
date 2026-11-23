// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/minimig/akiko_toc.h"

#include <algorithm>

#include "cores/minimig/akiko_subcode.h"
#include "infra/span_cut.h"

namespace mister::cores::minimig::akiko {

namespace {

using svc::disc::QChannel;
using svc::disc::QControl;
using svc::disc::QTimes;
using svc::disc::TocPoint;

constexpr svc::TrackCount kMaxTracks{99};

constexpr svc::TrackCount kMinRows{2};

QControl control_of(const Track& t) noexcept {
    return CdDisc::q_control(t);
}

QChannel point_row(TocPoint point, QControl control, svc::Msf at) noexcept {
    return QChannel::lead_in(control, point, QTimes{svc::Msf{}, at});
}

QChannel track_row(const Track& t) noexcept {
    return QChannel::lead_in(
        control_of(t),
        TocPoint::of_track(t.number),
        QTimes{svc::Msf{}, svc::Msf::of_lba_bcd_masked(t.start + svc::disc::kRedBookPregap)}
    );
}

}

void AkikoToc::clear() noexcept {
    TASTY_SEAT_BODY(AkikoToc);
    count_ = PointCount{};
    push_.reset();
}

void AkikoToc::add_(svc::disc::QChannel row) noexcept {
    if (count_ >= kPointLimit)
        return;
    infra::at_index(table_, infra::affine::end_of<PointIndex>(count_)) = row;
    count_ += PointCount{1};
}

void AkikoToc::build(std::span<const Track> rows) noexcept {
    TASTY_SEAT_BODY(AkikoToc);
    clear();
    const svc::TrackCount rows_count = svc::TrackCount::truncating(rows.size());
    if (rows_count < kMinRows)
        return;
    const svc::TrackCount real = infra::affine::saturating_sub(rows_count, svc::TrackCount{1});
    if (real > kMaxTracks)
        return;
    const QControl first = control_of(rows.front());

    add_(point_row(
        TocPoint::first_track_info(),
        first,
        svc::Msf{svc::TrackNumber{1}.bcd_byte(), 0, 0}
    ));
    add_(point_row(TocPoint::last_track_info(), first, svc::Msf{real.bcd_byte(), 0, 0}));
    add_(point_row(
        TocPoint::lead_out(),
        first,
        svc::Msf::of_lba_bcd_masked(
            infra::at_index(rows, real.next_index()).start + svc::disc::kRedBookPregap
        )
    ));
    for (svc::TrackIndex i{}; real.contains(i); i += svc::TrackCount{1})
        add_(track_row(infra::at_index(rows, i)));
}

std::optional<SubqReply> AkikoToc::next() noexcept {
    TASTY_SEAT_BODY(AkikoToc);
    if (!push_)
        return std::nullopt;
    const PointIndex at = push_->point();
    if (!infra::affine::in_bounds(count_, at)) {
        push_.reset();
        return std::nullopt;
    }
    SubqReply out = SubqReply::drip(
        infra::at_index(table_, at).with_relative(push_->lead_in_time())
    );
    *push_ = push_->next();
    return out;
}

namespace {

constexpr infra::ByteOffset kQChannelAt{12};

const Track& track_at(std::span<const Track> rows, proto::Lba lba) noexcept {
    const std::size_t real = rows.size() - 1;
    for (std::size_t i = 0; i < real; ++i) {
        const proto::Lba end = i + 1 < rows.size() ? rows[i + 1].start : rows[i].end;
        if (lba >= rows[i].start && lba < end)
            return rows[i];
    }
    return rows.front();
}

}

Subcode subcode_channels(std::span<const Track> rows, proto::Lba lba) noexcept {
    Subcode out{};
    if (rows.empty())
        return out;
    const Track& t = track_at(rows, lba);
    const proto::BlockCount rel = lba >= t.start ? lba - t.start : proto::BlockCount{};
    const QChannel row = QChannel::of_track(
        CdDisc::q_control(t),
        t.number,
        QTimes{
            svc::Msf::of_lba_bcd_masked(proto::Lba{} + rel),
            svc::Msf::of_lba_bcd_masked(lba + svc::disc::kRedBookPregap)
        }
    );
    row.store(infra::affine::from(std::span(out), kQChannelAt).first<QChannel::kBytes>());
    return out;
}

}
