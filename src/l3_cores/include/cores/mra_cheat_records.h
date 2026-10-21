// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "cores/cheat_geometry.h"
#include "cores/cheat_records.h"
#include "cores/mra_cheat.h"

namespace mister::cores::mra {

class MraCheatRecords final : public ICheatRecords {
public:
    MraCheatRecords() noexcept = default;

    MraCheatRecords(std::vector<MraCheat>&& cheats, CheatGeometry geom) noexcept
        : cheats_(std::move(cheats)),
          geom_(geom) {}

    [[nodiscard]] CheatGeometry cheat_geometry() const noexcept override { return geom_; }

    [[nodiscard]] std::size_t cheat_count() const noexcept override { return cheats_.size(); }

    [[nodiscard]] std::string_view cheat_name(std::size_t i) const noexcept override {
        return i < cheats_.size() ? std::string_view{cheats_[i].name} : std::string_view{};
    }

    [[nodiscard]] std::span<const std::uint8_t> cheat_bytes(std::size_t i) const noexcept override {
        if (i >= cheats_.size())
            return {};
        return {cheats_[i].bytes.data(), cheats_[i].bytes.size()};
    }

private:
    std::vector<MraCheat> cheats_;
    CheatGeometry geom_{};
};

}
