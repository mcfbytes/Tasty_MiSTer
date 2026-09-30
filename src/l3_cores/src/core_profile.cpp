// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/core_profile.h"

#include <cstring>

namespace mister::cores {

Ex<void> assert_services_match_census(const CoreProfile& p,
                                      std::span<const LinkDecoderDecl> census_rows) {
    if (p.services.size() != census_rows.size()) {
        return std::unexpected(
            Error{Errc::negotiation, ERR_SITE(), static_cast<std::uint32_t>(p.services.size())});
    }
    for (std::size_t i = 0; i < census_rows.size(); ++i) {
        const LinkDecoderDecl& a = p.services[i];
        const LinkDecoderDecl& b = census_rows[i];
        const bool same_name =
            (a.name == b.name) || (a.name && b.name && std::strcmp(a.name, b.name) == 0);
        if (!same_name || a.cause != b.cause || a.period_ms != b.period_ms || a.klass != b.klass) {
            return std::unexpected(
                Error{Errc::negotiation, ERR_SITE(), static_cast<std::uint32_t>(i)});
        }
    }
    return {};
}

}  // namespace mister::cores
