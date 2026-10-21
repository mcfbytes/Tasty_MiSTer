// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "cores/romset_alias_source.h"
#include "cores/romsets.h"
#include "infra/seat.h"

namespace mister::cores {

class NeoGeoAliasSource final : public IRomsetAliasSource {
    TASTY_SEAT_RESIDENT(Ui);

public:
    static constexpr std::uint64_t kDocMaxBytes = 1u << 20;

    [[nodiscard]] RomsetAlias query(
        IRomsetFiles& files,
        std::string_view dir,
        std::string_view home,
        std::string_view entry,
        std::string_view key
    ) override;

    std::uint32_t index_scans() const noexcept { return scans_; }

private:
    void ensure_index(IRomsetFiles& files, std::string_view dir, std::string_view home);

    std::string indexed_dir_;
    bool indexed_ = false;
    neogeo::RomsetIndex index_;
    std::uint32_t scans_ = 0;
};

}
