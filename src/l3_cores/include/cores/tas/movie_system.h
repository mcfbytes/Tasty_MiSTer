// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "cores/tas/movie_codec.h"
#include "cores/tas/movie_frame_tally.h"
#include "infra/error.h"
#include "infra/opt_ref.h"
#include "infra/prelude.h"

namespace mister::svc {
class IFile;
class Vfs;
}

namespace mister::cores::tas {

struct MovieSystem {
    std::string_view conf_str_name{};
    infra::OptRef<const MovieCodec> codec{};
};

[[nodiscard]] std::optional<MovieSystem> movie_system_for_path(
    std::string_view movie_path
) noexcept;
[[nodiscard]] std::optional<MovieSystem> movie_system_for(
    const svc::Vfs& vfs,
    std::string_view movie_path
) noexcept;

[[nodiscard]] std::optional<std::string> movie_bk2_platform(std::string_view header_text) noexcept;
[[nodiscard]] Ex<std::optional<std::string>> movie_bk2_platform(svc::IFile& movie_text);
[[nodiscard]] Ex<std::optional<std::string>> movie_bk2_platform(
    const svc::Vfs& vfs,
    std::string_view movie_path
);

[[nodiscard]] std::string movie_unplayable_reason(
    const Ex<std::optional<std::string>>& platform,
    std::string_view movie_path
);
[[nodiscard]] std::string movie_unplayable_reason(const svc::Vfs& vfs, std::string_view movie_path);
[[nodiscard]] Ex<MovieFacts> read_movie_facts(
    const svc::Vfs& vfs,
    const IMovieFormat& format,
    std::string_view movie_path
);

[[nodiscard]] MovieFrameTally count_movie_frames(
    const svc::Vfs& vfs,
    const IMovieFormat& format,
    std::string_view movie_path,
    const MovieFacts& facts
);
[[nodiscard]] bool movie_system_supported(const MovieSystem& sys) noexcept;
[[nodiscard]] bool movie_system_plays(std::string_view conf_str_name) noexcept;

}
