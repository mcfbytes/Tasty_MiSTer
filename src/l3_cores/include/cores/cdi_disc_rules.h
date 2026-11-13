// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>

#include "cores/cdi_block.h"
#include "svc/disc_geometry.h"
#include "svc/disc_rules.h"

namespace mister::cores::cdi {

enum class BlockAnswer : std::uint8_t { TocArea, Track, LeadOut };

[[nodiscard]] BlockAnswer classify(HeadLba lba, const svc::DiscGeometry& disc) noexcept;

template <DiscType Previous>
class CdiDiscRulesAfter final : public svc::IDiscRules {
public:
    [[nodiscard]] proto::BlockSize block_size() const noexcept override;

    void fill(
        const svc::DiscGeometry&,
        svc::IDiscReader&,
        proto::Lba,
        std::span<std::uint8_t>
    ) const noexcept override;
};

extern template class CdiDiscRulesAfter<DiscType::CdRom>;
extern template class CdiDiscRulesAfter<DiscType::CdRomXa>;

inline constexpr CdiDiscRulesAfter<DiscType::CdRom> kRulesAfterCdRom{};
inline constexpr CdiDiscRulesAfter<DiscType::CdRomXa> kRulesAfterXa{};

[[nodiscard]] const svc::IDiscRules& rules_after(DiscType previous) noexcept;

}
