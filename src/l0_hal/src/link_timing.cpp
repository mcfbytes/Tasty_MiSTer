// SPDX-License-Identifier: GPL-3.0-or-later
#include "hal/link_timing.h"

#include <expected>
#include <tuple>

#include "hal/fact_field.h"
#include "hal/program_geometry.h"

namespace mister::hal {

namespace {

template <class Values, class Group>
[[nodiscard]] constexpr bool resolve_rows(
    Values& values,
    const auto& rows,
    const Group& group,
    infra::ErrorSite site,
    Error& first_unmeasured
) noexcept {
    const auto attempt = [&](const auto& row) {
        const auto value = row.resolve(group, site);
        if (value) {
            row.fill(values, *value);
        } else {
            first_unmeasured = value.error();
        }
        return value.has_value();
    };
    return std::apply([&](const auto&... row) { return (attempt(row) && ...); }, rows);
}

}

Ex<LinkTimingValues> LinkTimingValues::resolve(
    const LinkTiming& t,
    infra::ErrorSite site
) noexcept {
    LinkTimingValues values{};
    Error first_unmeasured{};
    if (!resolve_rows(values, kFields, t, site, first_unmeasured))
        return std::unexpected(first_unmeasured);
    return values;
}

Ex<ProgramGeometryValues> ProgramGeometryValues::resolve(
    const ProgramGeometry& g,
    infra::ErrorSite site
) noexcept {
    ProgramGeometryValues values{};
    Error first_unmeasured{};
    if (!resolve_rows(values, kFields, g, site, first_unmeasured))
        return std::unexpected(first_unmeasured);
    return values;
}

}
