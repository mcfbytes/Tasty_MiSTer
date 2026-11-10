// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "svc/disc_identity.h"
#include "svc/disc_identity_role.h"

namespace mister::cores {

class PsxDiscIdentity final : public svc::IDiscIdentity {
public:
    constexpr PsxDiscIdentity() noexcept = default;

    [[nodiscard]] svc::DiscIdentity identify(
        const svc::DiscGeometry& disc,
        svc::IDiscReader& reader
    ) const noexcept override;
};

inline constexpr PsxDiscIdentity kPsxDiscIdentity{};

}
