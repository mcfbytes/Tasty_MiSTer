// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

#include "cores/n64_db.h"
#include "cores/n64_db_reader.h"
#include "cores/n64_db_tags.h"
#include "cores/n64_ipl3.h"
#include "cores/n64_load_facts.h"
#include "cores/n64_options.h"
#include "cores/n64_rom.h"
#include "infra/fixed_str.h"
#include "infra/seat.h"
#include "svc/types.h"
#include "cores/n64_rom_facts.h"

namespace mister::cores::n64 {

struct DetectOutcome {
    DetectFlags detected{};
    bool homebrew = false;
};

class DetectLadder final {
    TASTY_SEAT_EXEMPT(component);

public:
    static constexpr std::size_t kInfoChars = 255;

    explicit DetectLadder(N64DbReader& reader) noexcept : reader_(reader) {}

    [[nodiscard]] DetectOutcome run(const RomFacts& facts, LoadFacts& slab);

    void reset() noexcept {
        match_.reset();
        detected_ = DetectFlags{};
        info_.clear();
    }

    void compose_info(const RomFacts& facts, const LoadFacts& slab, bool patched);

    [[nodiscard]] std::string_view info() const noexcept { return info_.view(); }

    [[nodiscard]] const std::optional<DbMatch>& match() const noexcept { return match_; }

private:
    [[nodiscard]] DetectFlags lookup_and_apply_(LoadFacts& slab, const DbKey& key);
    void apply_tags_(LoadFacts& slab, const DbTags& t);
    [[nodiscard]] bool apply_homebrew_(const RomFacts& facts, LoadFacts& slab);
    void apply_unknown_defaults_(LoadFacts& slab);
    void apply_ipl3_(LoadFacts& slab, const Ipl3Detect& d, DetectFlags& detected);

    N64DbReader& reader_;
    std::optional<DbMatch> match_{};
    DetectFlags detected_{};
    FixedStr<kInfoChars + 1, StrFit::Clip> info_{};
};

}
