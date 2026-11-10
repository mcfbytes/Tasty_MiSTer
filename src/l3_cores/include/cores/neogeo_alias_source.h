// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>

#include "cores/romset_alias_source.h"
#include "cores/romset_index.h"
#include "infra/seat.h"

namespace mister::cores {

class NeoGeoAliasSource final : public IRomsetAliasSource {
    TASTY_SEAT_RESIDENT(Ui);

public:
    static constexpr std::uint64_t kDocMaxBytes = 1u << 20;

    [[nodiscard]] RomsetAlias query(
        IRomsetFiles& files,
        BrowseDirs dirs,
        EntryLookup entry
    ) override;

    std::uint32_t index_scans() const noexcept { return scans_; }

private:
    void ensure_index(IRomsetFiles& files, BrowseDirs dirs);

    std::string indexed_dir_;
    bool indexed_ = false;
    neogeo::RomsetIndex index_;
    std::uint32_t scans_ = 0;
};

}
