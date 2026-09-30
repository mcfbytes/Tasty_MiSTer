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
};

static_assert(static_cast<std::uint16_t>(Errc::unimplemented) == 22 &&
              static_cast<std::uint16_t>(Errc::aperture_range) == 23 &&
              static_cast<std::uint16_t>(Errc::stale) == 24 &&
              static_cast<std::uint16_t>(Errc::busy) == 25);

struct Error {
    Errc code;
    std::uint16_t site;
    std::uint32_t detail;
};
static_assert(std::is_trivially_copyable_v<Error> && sizeof(Error) == 8);

template <class T>
using Ex = std::expected<T, Error>;

#define ERR_SITE() (static_cast<std::uint16_t>(__LINE__))

[[noreturn]] void fatal(Error e, const char* what);

inline std::unexpected<Error> unimplemented(std::uint16_t site) {
    return std::unexpected(Error{Errc::unimplemented, site, 0});
}

}  // namespace mister
