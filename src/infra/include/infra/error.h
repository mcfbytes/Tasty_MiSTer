// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <expected>
#include <type_traits>

namespace mister {

enum class Errc : std::uint16_t {

    spi_timeout,
    spi_nak,
    fpga_not_ready,

    io,
    not_found,
    short_read,
    short_write,
    bad_format,
    mount_failed,

    bad_confstr,
    bad_opcode,
    slot_range,
    negotiation,

    mmap_failed,
    uio_open,
    dt_missing,
    bridge_state,
    os,

    core_load,
    cancelled,
    would_block,
    timeout,
    unimplemented,

    aperture_range,
    stale,
    busy,
    seat_absent,
};

static_assert(
    static_cast<std::uint16_t>(Errc::unimplemented) == 22
    && static_cast<std::uint16_t>(Errc::aperture_range) == 23
    && static_cast<std::uint16_t>(Errc::stale) == 24
    && static_cast<std::uint16_t>(Errc::busy) == 25
    && static_cast<std::uint16_t>(Errc::seat_absent) == 26
);

namespace infra {

enum class ErrorSite : std::uint16_t {};

inline constexpr ErrorSite kNoSite{};

[[nodiscard]] constexpr ErrorSite site_at_line(int line) noexcept {
    return static_cast<ErrorSite>(line);
}

}

struct Error {
    Errc code;
    infra::ErrorSite site;
    std::uint32_t detail;
};

static_assert(std::is_trivially_copyable_v<Error> && sizeof(Error) == 8);

template <class T>
using Ex = std::expected<T, Error>;

#define ERR_SITE() (::mister::infra::site_at_line(__LINE__))

namespace infra {

[[nodiscard]] constexpr Error errno_error(Errc errc, ErrorSite at, int err) noexcept {
    return Error{errc, at, static_cast<std::uint32_t>(err)};
}

}

[[noreturn]] void fatal(Error e, const char* what);

inline std::unexpected<Error> unimplemented(infra::ErrorSite site) {
    return std::unexpected(Error{Errc::unimplemented, site, 0});
}

}
