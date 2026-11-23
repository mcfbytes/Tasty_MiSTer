// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string_view>

#include "infra/error.h"
#include "infra/path_text.h"
#include "infra/prelude.h"
#include "proto/types.h"
#include "svc/disk_geometry.h"

namespace mister::svc {

struct VhdSidecar {

    enum class Refusal : std::uint8_t { InvalidConfig = 1, MissingConfig, InvalidSize };

    DiskGeometry::Sectors sectors{};
    DiskGeometry::Heads heads{};
    DiskGeometry::Cylinders cylinders{};

    [[nodiscard]] static constexpr Error refused(Refusal why, infra::ErrorSite at) noexcept {
        return Error{Errc::bad_format, at, static_cast<std::uint32_t>(why)};
    }

    [[nodiscard]] static constexpr std::optional<Refusal> refusal_of(const Error& e) noexcept {
        for (const Refusal why :
             {Refusal::InvalidConfig, Refusal::MissingConfig, Refusal::InvalidSize})
            if (e == refused(why, e.site))
                return why;
        return std::nullopt;
    }

    friend constexpr bool operator==(const VhdSidecar&, const VhdSidecar&) = default;
};

[[nodiscard]] std::optional<infra::PathText> vhd_sidecar_path(std::string_view image_path) noexcept;

[[nodiscard]] Ex<VhdSidecar> parse_vhd_sidecar(std::string_view text) noexcept;

[[nodiscard]] Ex<proto::BlockCount> apply_vhd_sidecar(
    const VhdSidecar& sidecar,
    proto::FileSize image
) noexcept;

}
