// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

#include "infra/affine.h"
#include "infra/indexed_array.h"
#include "proto/types.h"
#include "svc/disc/disc_ask.h"
#include "svc/disc/disc_reader.h"
#include "svc/disc/q_channel.h"
#include "svc/disc/track.h"
#include "svc/types.h"

namespace mister::cores::minimig {

using svc::disc::Track;

using RawFrame = std::span<std::byte, svc::disc::kRawFrameBytes>;

struct CdDisc {

    enum class Image : std::uint8_t { Cue, Chd };

    enum class Gap : std::uint8_t { Own, Previous };

    std::vector<Track> tracks;
    std::unique_ptr<svc::disc::IDiscReader> reader;
    Image image = Image::Cue;

    [[nodiscard]] bool chd() const noexcept { return image == Image::Chd; }

    [[nodiscard]] std::span<const Track> rows() const noexcept { return tracks; }

    [[nodiscard]] bool read(proto::Lba lba, RawFrame out) const {
        if (!reader)
            return false;
        const auto got = reader->read_form(svc::disc::DiscForm::RawFrame, lba, out);
        return got && got->size() == out.size();
    }

    [[nodiscard]] bool read_audio(proto::Lba lba, RawFrame out, Gap gap) const {
        if (!chd())
            return false;
        const std::optional<svc::TrackIndex> at = track_at(rows(), lba, gap);
        if (!at)
            return false;
        const Track& t = infra::at_index(tracks, *at);
        if (data(t) || t.sector_size != svc::disc::kRawFrameBytes)
            return false;
        return read(lba, out);
    }

    [[nodiscard]] static constexpr bool data(const Track& t) noexcept {
        return t.type != svc::disc::TrackType::Cdda;
    }

    [[nodiscard]] static constexpr bool holds(const Track& t, proto::Lba lba) noexcept {
        return lba >= t.start && lba < t.end;
    }

    [[nodiscard]] static constexpr svc::disc::QControl q_control(const Track& t) noexcept {
        return svc::disc::QControl::of(t.type);
    }

    [[nodiscard]] static constexpr std::optional<svc::TrackIndex> track_at(
        std::span<const Track> rows,
        proto::Lba lba,
        Gap gap
    ) noexcept {
        const svc::TrackCount real = infra::affine::saturating_sub(
            svc::TrackCount::truncating(rows.size()),
            svc::TrackCount{1}
        );
        for (svc::TrackIndex i{}; real.contains(i); i += svc::TrackCount{1}) {
            const Track& row = infra::at_index(rows, i);
            if (holds(row, lba))
                return i;
            const svc::TrackIndex next = i + svc::TrackCount{1};
            if (gap == Gap::Previous
                && real.contains(next)
                && lba >= row.end
                && lba < infra::at_index(rows, next).start)
                return i;
        }
        return std::nullopt;
    }
};

}
