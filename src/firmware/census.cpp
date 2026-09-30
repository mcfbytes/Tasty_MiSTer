// SPDX-License-Identifier: GPL-3.0-or-later
#include "census.h"

#include "cores/core_profile.h"
#include "cores/registry.h"

namespace mister::fw {

Ex<void> verify_census() {
    const std::span<const cores::CoreFactory> table = cores::core_table();

    for (std::size_t i = 0; i < table.size(); ++i) {
        const cores::CoreProfile* p = table[i].profile;
        if (p == nullptr || p->services.empty()) continue;
        const DocCensus* doc = nullptr;
        for (const DocCensus& d : kDocCensus) {
            if (d.core == p->name) {
                doc = &d;
                break;
            }
        }
        if (doc == nullptr) {
            return std::unexpected(
                Error{Errc::negotiation, ERR_SITE(), static_cast<std::uint32_t>(i)});
        }
        if (auto r = cores::assert_services_match_census(*p, doc->rows); !r) {
            return r;
        }
    }

    for (std::size_t i = 0; i < kDocCensus.size(); ++i) {
        bool found = false;
        for (const cores::CoreFactory& f : table) {
            if (f.profile != nullptr && f.profile->name == kDocCensus[i].core) {
                found = true;
                break;
            }
        }
        if (!found) {
            return std::unexpected(
                Error{Errc::negotiation, ERR_SITE(), static_cast<std::uint32_t>(i)});
        }
    }
    return {};
}

}  // namespace mister::fw
