// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "infra/fixed_str.h"
#include "infra/indexed_array.h"
#include "infra/opt_ref.h"
#include "infra/path_text.h"
#include "infra/seat.h"
#include "svc/types.h"

namespace mister::cores::mdp {

class CueSheet {
    TASTY_SEAT_EXEMPT(component);

public:
    static constexpr std::uint8_t kMaxTracks = 99;

    static constexpr std::size_t kPathMax = infra::kPathMax;

    static constexpr std::size_t kLineMax = 1023;

    struct Track {
        FixedStr<kPathMax, StrFit::Clip> wav_path{};
        bool loops = false;

        std::uint32_t loop_sector = 0;
    };

    void parse(std::string_view text, std::string_view base_dir) noexcept;

    [[nodiscard]] svc::TrackCount track_count() const noexcept { return num_tracks_; }

    [[nodiscard]] infra::OptRef<const Track> track(svc::TrackNumber n) const noexcept {
        if (!num_tracks_.contains(n.index()))
            return {};
        return infra::OptRef<const Track>{tracks_[n]};
    }

private:
    infra::IndexedArray<Track, kMaxTracks + 1, svc::TrackNumber> tracks_{};
    svc::TrackCount num_tracks_{};
};

}
