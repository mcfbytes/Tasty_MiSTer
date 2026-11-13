// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>

#include "svc/disc_identity.h"
#include "svc/disc_rules.h"

namespace mister::cores {

class PsxDiscRules final : public svc::IDiscRules {
public:
    constexpr PsxDiscRules() noexcept = default;

    [[nodiscard]] svc::DiscIdentity identify(
        const svc::DiscGeometry&,
        svc::IDiscReader&
    ) const noexcept override;

    [[nodiscard]] proto::BlockSize block_size() const noexcept override;

    void fill(
        const svc::DiscGeometry&,
        svc::IDiscReader&,
        proto::Lba,
        std::span<std::uint8_t>
    ) const noexcept override;
};

inline constexpr PsxDiscRules kPsxDiscRules{};

}
