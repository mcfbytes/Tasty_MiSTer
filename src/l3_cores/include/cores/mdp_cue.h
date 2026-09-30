// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <string_view>

#include "infra/fixed_str.h"
#include "infra/seat.h"

namespace mister::cores::mdp {

class CueSheet {
    TASTY_SEAT_EXEMPT(component);

public:
    static constexpr std::uint8_t kMaxTracks = 99;

    static constexpr std::size_t kPathMax = 1024;

    static constexpr std::size_t kLineMax = 1023;

    struct Track {
        FixedStr<kPathMax, StrFit::Clip> wav_path{};
        bool loops = false;

        std::uint32_t loop_sector = 0;
    };

    void parse(std::string_view text, std::string_view base_dir) noexcept;

    [[nodiscard]] std::uint8_t track_count() const noexcept { return num_tracks_; }

    [[nodiscard]] const Track* track(std::uint8_t n) const noexcept {
        if (n < 1 || n > num_tracks_) return nullptr;
        return &tracks_[n];
    }

private:
    std::array<Track, kMaxTracks + 1> tracks_{};
    std::uint8_t num_tracks_ = 0;
};

}  // namespace mister::cores::mdp
