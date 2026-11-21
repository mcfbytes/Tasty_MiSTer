// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>

#include "svc/disc/disc_identity.h"
#include "svc/disc/disc_rules.h"

namespace mister::cores::psx {

class PsxDiscRules final : public svc::disc::IDiscRules {
public:
    constexpr PsxDiscRules() noexcept = default;

    [[nodiscard]] svc::disc::DiscIdentity identify(
        const svc::disc::DiscGeometry&,
        svc::disc::IDiscReader&
    ) const noexcept override;

    [[nodiscard]] proto::BlockSize block_size() const noexcept override;

    void fill(
        const svc::disc::DiscGeometry&,
        svc::disc::IDiscReader&,
        proto::Lba,
        std::span<std::uint8_t>
    ) const noexcept override;
};

inline constexpr PsxDiscRules kPsxDiscRules{};

}
