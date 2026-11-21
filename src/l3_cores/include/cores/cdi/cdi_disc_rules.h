// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>

#include "cores/cdi/cdi_block.h"
#include "svc/disc/disc_geometry.h"
#include "svc/disc/disc_rules.h"

namespace mister::cores::cdi {

enum class BlockAnswer : std::uint8_t { TocArea, Track, LeadOut };

[[nodiscard]] BlockAnswer classify(HeadLba lba, const svc::disc::DiscGeometry& disc) noexcept;

template <DiscType Previous>
class CdiDiscRulesAfter final : public svc::disc::IDiscRules {
public:
    [[nodiscard]] proto::BlockSize block_size() const noexcept override;

    void fill(
        const svc::disc::DiscGeometry&,
        svc::disc::IDiscReader&,
        proto::Lba,
        std::span<std::uint8_t>
    ) const noexcept override;
};

extern template class CdiDiscRulesAfter<DiscType::CdRom>;
extern template class CdiDiscRulesAfter<DiscType::CdRomXa>;

inline constexpr CdiDiscRulesAfter<DiscType::CdRom> kRulesAfterCdRom{};
inline constexpr CdiDiscRulesAfter<DiscType::CdRomXa> kRulesAfterXa{};

[[nodiscard]] const svc::disc::IDiscRules& rules_after(DiscType previous) noexcept;

}
