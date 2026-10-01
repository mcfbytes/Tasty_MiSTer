// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "cores/core_profile.h"
#include "cores/movie_codec.h"
#include "infra/error.h"

namespace mister::svc {
class IFile;
class Vfs;
}  // namespace mister::svc

namespace mister::cores {

struct MovieSystem {
    CoreKind kind = CoreKind::Generic;
    std::string_view conf_str_name{};
    const IMovieCodec* codec = nullptr;
};

[[nodiscard]] std::optional<MovieSystem> movie_system_for_path(
    std::string_view movie_path) noexcept;
[[nodiscard]] std::optional<MovieSystem> movie_system_for(const svc::Vfs& vfs,
                                                          std::string_view movie_path) noexcept;

[[nodiscard]] std::optional<std::string> movie_bk2_platform(std::string_view header_text) noexcept;
[[nodiscard]] Ex<std::optional<std::string>> movie_bk2_platform(svc::IFile& movie_text);
[[nodiscard]] Ex<std::optional<std::string>> movie_bk2_platform(const svc::Vfs& vfs,
                                                                std::string_view movie_path);

[[nodiscard]] std::string movie_unplayable_reason(const Ex<std::optional<std::string>>& platform,
                                                  std::string_view movie_path);
[[nodiscard]] std::string movie_unplayable_reason(const svc::Vfs& vfs, std::string_view movie_path);
[[nodiscard]] Ex<IMovieCodec::Facts> read_movie_facts(const svc::Vfs& vfs, const IMovieCodec& codec,
                                                      std::string_view movie_path);
[[nodiscard]] bool movie_system_supported(const MovieSystem& sys) noexcept;
[[nodiscard]] bool movie_system_plays(std::string_view conf_str_name) noexcept;

}  // namespace mister::cores
