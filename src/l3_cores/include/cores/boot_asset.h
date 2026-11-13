// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "cores/types.h"
#include "svc/search_policy.h"

namespace mister::cores {

enum class AssetAnchor : std::uint8_t {
    Search,
    ImageDir,
    ImageParentDir,
    CoreHome,
};

enum class AssetWait : std::uint8_t { Ready, Miss, Pending };

enum class BootGroup : std::uint8_t {
    First,
    Second,
    Third,
};

struct BootAsset {
    std::string_view name;
    std::span<const svc::SearchDir> where;
    bool optional = true;
    AssetAnchor anchor = AssetAnchor::Search;
    std::string_view subdir{};
    WideIoIndex dest{};
    BootGroup group = BootGroup::First;
    bool group_required = false;

    std::optional<FileSize> exact_size{};

    [[nodiscard]] constexpr bool accepts(FileSize size) const noexcept {
        return !exact_size || *exact_size == size;
    }
};

inline constexpr std::size_t kMaxBootBuffers = 3;

inline constexpr std::size_t kMaxStartAssets = 5;

inline constexpr svc::SearchDir kCoreHomeDir[] = {svc::SearchDir::CoreDir};

consteval std::array<BootAsset, 5> stock_start_chain(
    std::string_view subdir,
    AssetAnchor anchor = AssetAnchor::Search
) {
    const auto row = [subdir, anchor](std::string_view name, std::uint8_t sub) consteval {
        return BootAsset{
            .name = name,
            .where = kCoreHomeDir,
            .anchor = anchor,
            .subdir = subdir,
            .dest = WideIoIndex::of(IoIndex{}, sub)
        };
    };
    return {
        {row("boot0.rom", 0),
         row("boot1.rom", 1),
         row("boot2.rom", 2),
         row("boot3.rom", 3),
         row("boot.rom", 0)}
    };
}

static_assert(
    stock_start_chain({})[1].dest == WideIoIndex{0x40}
    && stock_start_chain({})[2].dest == WideIoIndex{0x80}
    && stock_start_chain({})[3].dest == WideIoIndex{0xC0}
);

inline constexpr auto kCoreHomeChain = stock_start_chain({}, AssetAnchor::CoreHome);
static_assert(kCoreHomeChain.size() <= kMaxStartAssets);

[[nodiscard]] constexpr bool start_row_sends(const BootAsset& row, bool index0_taken) noexcept {
    return !(index0_taken && row.dest == WideIoIndex{});
}

}
