// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>
#include <utility>

#include "hal/reg_word.h"
#include "hal/sys_directory.h"
#include "hal/types.h"
#include "infra/error.h"

namespace mister::hal {

struct DirCount {
    std::uint32_t v = 0;

    [[nodiscard]] static constexpr DirCount of(RegWord w) noexcept { return DirCount{w.bits()}; }

    [[nodiscard]] static constexpr DirCount preceding(SysDirectory::EntryIndex i) noexcept {
        return DirCount{std::to_underlying(i)};
    }

    [[nodiscard]] constexpr bool within_max() const noexcept {
        return v <= SysDirectory::kMaxEntries;
    }

    [[nodiscard]] constexpr std::optional<LwExtent> span(LwExtent stride) const noexcept {
        if (!within_max())
            return std::nullopt;
        return LwExtent{stride.v * v};
    }

    [[nodiscard]] constexpr bool empty() const noexcept { return v == 0; }

    [[nodiscard]] constexpr bool covers(SysDirectory::EntryIndex i) const noexcept {
        return static_cast<std::uint32_t>(i) < v;
    }

    [[nodiscard]] constexpr Error refusal(Errc why, infra::ErrorSite at) const noexcept {
        return Error{why, at, v};
    }

    friend constexpr bool operator==(DirCount, DirCount) = default;
};

}
