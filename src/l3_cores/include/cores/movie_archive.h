// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>

#include "infra/error.h"

namespace mister::svc {
class IArchive;
class IFile;
class Vfs;
}  // namespace mister::svc

namespace mister::cores {

struct MovieArchiveFormat {
    enum class Use : std::uint8_t { Skip, Lines, Json, Value, Presence, Log };
    enum class Naming : std::uint8_t { Exact, BizHawkLump };
    struct Rule {
        std::string_view stem;
        Use use = Use::Skip;
    };

    static constexpr std::size_t kMembersMax = 256;
    static constexpr std::uint64_t kLinesMax = 32 * 1024;
    static constexpr std::uint64_t kJsonMax = 16 * 1024;
    static constexpr std::uint64_t kValueMax = 192;
    static constexpr std::size_t kHeadMax = 64 * 1024;
    static constexpr std::uint64_t kLogMax = 64u << 20;
    static constexpr std::size_t kJsonDepth = 8;

    std::span<const Rule> rules;
    Use others = Use::Presence;
    Naming naming = Naming::Exact;
};

inline constexpr std::array<MovieArchiveFormat::Rule, 5> kBk2Rules{{
    {"Header", MovieArchiveFormat::Use::Lines},
    {"SyncSettings", MovieArchiveFormat::Use::Json},
    {"Input Log", MovieArchiveFormat::Use::Log},
    {"Comments", MovieArchiveFormat::Use::Skip},
    {"Subtitles", MovieArchiveFormat::Use::Skip},
}};
inline constexpr MovieArchiveFormat kBk2Archive{.rules = kBk2Rules,
                                                .others = MovieArchiveFormat::Use::Presence,
                                                .naming = MovieArchiveFormat::Naming::BizHawkLump};
inline constexpr std::array<MovieArchiveFormat::Rule, 1> kLsmvRules{{
    {"input", MovieArchiveFormat::Use::Log},
}};
inline constexpr MovieArchiveFormat kLsmvArchive{.rules = kLsmvRules,
                                                 .others = MovieArchiveFormat::Use::Value,
                                                 .naming = MovieArchiveFormat::Naming::Exact};

[[nodiscard]] Ex<std::unique_ptr<svc::IFile>> open_movie_archive(
    std::unique_ptr<svc::IArchive> archive, const MovieArchiveFormat& format);
[[nodiscard]] Ex<std::unique_ptr<svc::IFile>> open_movie_archive(const svc::Vfs& vfs,
                                                                 std::string_view path,
                                                                 const MovieArchiveFormat& format);

[[nodiscard]] Ex<std::string> flatten_json(std::string_view json, std::string_view prefix,
                                           std::size_t cap);

}  // namespace mister::cores
