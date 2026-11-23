// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <utility>

#include "infra/prelude.h"

namespace mister::svc {

enum class DiskCodec : std::uint8_t {
    Raw,
    Nib,
    GcrSectors,
    GcrTracks,
    T64,
    Woz525,
    Woz35,
    ProDosBlocks,
    Atx,
    Adf,
    kCount,
};

[[nodiscard]] constexpr Error unserved_codec(DiskCodec codec, infra::ErrorSite at) noexcept {
    return Error{Errc::bad_format, at, std::to_underlying(codec)};
}

[[nodiscard]] constexpr bool admits_by_content(DiskCodec c) noexcept {
    switch (c) {
        case DiskCodec::Woz525:
        case DiskCodec::Woz35:
        case DiskCodec::ProDosBlocks:
            return true;
        case DiskCodec::Raw:
        case DiskCodec::Nib:
        case DiskCodec::GcrSectors:
        case DiskCodec::GcrTracks:
        case DiskCodec::T64:
        case DiskCodec::Atx:
        case DiskCodec::Adf:
        case DiskCodec::kCount:
            break;
    }
    return false;
}

}
