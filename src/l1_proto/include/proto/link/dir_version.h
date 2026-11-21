// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <utility>

#include "hal/sys_directory.h"
#include "infra/error.h"
#include "proto/dir_entry_kind.h"
#include "infra/prelude.h"

namespace mister::proto::link {

enum class DirMajor : std::uint16_t { V1 = 1, V2 = 2 };

class DirVersion {
public:
    constexpr DirVersion() noexcept = default;

    [[nodiscard]] static DirVersion of_wire(hal::RegWord word) noexcept;

    [[nodiscard]] Error refusal(Errc why, infra::ErrorSite at) const noexcept;
    [[nodiscard]] bool has_major(DirMajor m) const noexcept;

    [[nodiscard]] bool is_base_minor() const noexcept;
    friend bool operator==(DirVersion, DirVersion) = default;

private:
    std::uint32_t v = 0;
};

static_assert(std::to_underlying(DirMajor::V2) == hal::SysDirectory::kMajor);

inline constexpr std::array<std::pair<DirEntryKind, DirMajor>, 5> kKnownMajors{{
    {DirEntryKind::Pad, DirMajor::V1},
    {DirEntryKind::Sector, DirMajor::V2},
    {DirEntryKind::Osd, DirMajor::V2},
    {DirEntryKind::Sys, DirMajor::V2},
    {DirEntryKind::Lane, DirMajor::V1},
}};

[[nodiscard]] constexpr DirMajor kKnownMajor(DirEntryKind kind) noexcept {
    for (const auto& [at, major] : kKnownMajors)
        if (at == kind)
            return major;
    return DirMajor::V1;
}

}
