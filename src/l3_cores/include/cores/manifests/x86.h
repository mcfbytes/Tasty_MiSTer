// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string_view>

#include "cores/core_profile.h"

namespace mister::cores::manifests {

consteval CoreProfile own_block_service_profile(
    CoreKind kind,
    std::string_view name,
    bool image_rows_no_zip
) {
    return CoreProfile{
        .kind = kind,
        .name = name,
        .rbf = "",
        .services = {},
        .slots = {},
        .boot_assets = {},
        .start = StartRule::None,
        .signatures = {},
        .block_service = BlockService::CoreOwn,
        .image_rows_no_zip = image_rows_no_zip,
        .undeclared_start_mount = StartMount::Unmodelled,
    };
}

inline constexpr CoreProfile kAo486 = own_block_service_profile(CoreKind::Ao486, "AO486", true);
inline constexpr CoreProfile kPc110 = own_block_service_profile(CoreKind::Pc110, "PC110", true);
inline constexpr CoreProfile kZ486 = own_block_service_profile(CoreKind::Z486, "Z486", true);
inline constexpr CoreProfile kZ386 = own_block_service_profile(CoreKind::Z386, "Z386", true);
inline constexpr CoreProfile kPcxt = own_block_service_profile(CoreKind::Pcxt, "PCXT", false);
inline constexpr CoreProfile kTandy1000 = own_block_service_profile(
    CoreKind::Tandy1000,
    "Tandy1000",
    false
);
inline constexpr CoreProfile kPcjr = own_block_service_profile(CoreKind::Pcjr, "PCjr", false);
inline constexpr CoreProfile kPcxtEga = own_block_service_profile(
    CoreKind::PcxtEga,
    "PCXT-EGA",
    false
);

}
