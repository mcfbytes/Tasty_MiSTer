// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>

#include "infra/error.h"

namespace mister::cores {

enum class MovieRefusal : std::uint8_t {
    None,
    NotAMovie,
    Savestate,
    Binary,
    Disk,
    PortType,
    Multitap,
    Checksum,
    Command,
    TooLong,
    BadLine,
    Setting,
    Archive,
    Subframe,
    System,
    ChdUnsupported,
    kCount,
};

[[nodiscard]] constexpr Error movie_refusal(MovieRefusal r, infra::ErrorSite site) noexcept {
    return Error{Errc::bad_format, site, static_cast<std::uint32_t>(r)};
}

[[nodiscard]] constexpr std::optional<MovieRefusal> movie_refusal_of(const Error& e) noexcept {
    if (e.code != Errc::bad_format)
        return std::nullopt;
    if (e.detail >= static_cast<std::uint32_t>(MovieRefusal::kCount))
        return std::nullopt;
    return static_cast<MovieRefusal>(e.detail);
}

}
