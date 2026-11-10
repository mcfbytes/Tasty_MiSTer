// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <span>
#include <string_view>

#include "cores/core_profile.h"
#include "cores/movie_codec.h"

namespace mister::cores {

struct MovieCodecRow {
    CoreKind kind;
    std::string_view name;
    std::string_view ext;
    std::string_view bk2_platform;
    std::string_view display_name;
    MovieCodec codec;
};

[[nodiscard]] std::string_view movie_path_extension(std::string_view movie_path) noexcept;

[[nodiscard]] std::span<const MovieCodecRow> movie_codec_rows() noexcept;

[[nodiscard]] const MovieCodecRow* unique_row_for_ext(std::string_view movie_path) noexcept;

}
