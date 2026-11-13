// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <utility>

#include "hal/sys_directory.h"
#include "infra/error.h"

namespace mister::proto {

enum class DirMajor : std::uint16_t { V1 = 1 };

class DirVersion {
public:
    constexpr DirVersion() noexcept = default;

    [[nodiscard]] static DirVersion of_wire(std::uint32_t word) noexcept;

    [[nodiscard]] Error refused(Errc why, infra::ErrorSite at) const noexcept;
    [[nodiscard]] bool has_major(DirMajor m) const noexcept;

    [[nodiscard]] bool is_base_minor() const noexcept;
    friend bool operator==(DirVersion, DirVersion) = default;

private:
    std::uint32_t v = 0;
};

static_assert(std::to_underlying(DirMajor::V1) == hal::SysDirectory::kMajor);

}
