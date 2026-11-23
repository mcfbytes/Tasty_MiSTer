// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>

#include "cores/save_filter.h"
#include "infra/static_bytes.h"
#include "proto/types.h"
#include "svc/save_banks.h"

namespace mister::cores {

struct SaveFrame {
    SaveFilter filter = SaveFilter::Whole;
    svc::SaveBanks banks{};

    infra::StaticBytes heads{};

    [[nodiscard]] constexpr bool frames(
        proto::FileSize wire,
        std::span<const std::uint8_t> lead
    ) const noexcept {
        if (!banks.framed())
            return lead.empty() && heads.empty() && filter == SaveFilter::Whole;
        return banks.whole_bodies(wire)
            && proto::FileSize::of(lead) == banks.lead
            && proto::FileSize::of(heads.view()) == banks.heads_bytes(wire);
    }
};

}
