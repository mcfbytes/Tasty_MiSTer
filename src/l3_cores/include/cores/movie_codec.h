// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

#include "cores/i_movie_format.h"
#include "cores/i_replay_core.h"
#include "cores/movie_command_mask.h"
#include "cores/movie_facts.h"
#include "cores/movie_frame.h"
#include "cores/movie_region.h"
#include "cores/movie_refusal.h"
#include "cores/movie_rerecord_count.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "proto/types.h"

namespace mister::cores {

inline constexpr std::size_t kMovieLineMax = 256;

inline constexpr proto::FileSize kCopierGrid{1024};
inline constexpr proto::FileSize kCopierHeader{512};
inline constexpr proto::FileSize kSmdHeader{128};

[[nodiscard]] constexpr proto::FileSize tail_remainder(
    proto::FileSize size,
    proto::FileSize grid
) noexcept {
    return size - proto::FileSize{grid.length_of(size.whole_units(grid))};
}

[[nodiscard]] constexpr proto::FileSize copier_header(proto::FileSize size) noexcept {
    const proto::FileSize rest = tail_remainder(size, kCopierGrid);
    if (rest == kCopierHeader || rest == kSmdHeader)
        return rest;
    return {};
}

[[nodiscard]] std::optional<MovieRerecordCount> rerecord_count(
    std::string_view key,
    std::string_view val
) noexcept;

class MovieCodec {
    TASTY_SEAT_EXEMPT(const_shared);

public:
    constexpr MovieCodec(const IReplayCore& replay, const IMovieFormat& grammar) noexcept
        : replay_(replay),
          grammar_(grammar) {}

    [[nodiscard]] constexpr const IReplayCore& core() const noexcept { return replay_; }

    [[nodiscard]] constexpr const IMovieFormat& format() const noexcept { return grammar_; }

private:
    const IReplayCore& replay_;
    const IMovieFormat& grammar_;
};

[[nodiscard]] constexpr IMovieFormat::DigestSpan span_past(
    proto::FileSize size,
    proto::FileSize header
) noexcept {
    return IMovieFormat::DigestSpan{.offset = header, .length = size - header};
}

[[nodiscard]] infra::OptRef<const MovieCodec> movie_codec_for(
    std::string_view conf_str_name,
    std::string_view movie_path
) noexcept;

[[nodiscard]] infra::OptRef<const MovieCodec> movie_codec_for_path(
    std::string_view movie_path
) noexcept;

}
