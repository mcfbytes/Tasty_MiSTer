// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "proto/types.h"
#include "svc/vfs.h"

namespace mister::cores {

using proto::WideIoIndex;

enum class AssetAnchor : std::uint8_t {
    Search,
    ImageDir,
    ImageParentDir,
    CoreHome,
};

enum class AssetLoader : std::uint8_t { GenericFileTx, PceBios };

enum class AssetWait : std::uint8_t { Ready, Miss, Pending };

struct BootAsset {
    std::string_view name;
    std::span<const svc::SearchDir> where;
    bool optional = true;
    AssetAnchor anchor = AssetAnchor::Search;
    std::string_view subdir{};
    WideIoIndex dest{};
    std::uint8_t group = 0;
    bool group_required = false;
    std::string_view marker_sibling{};
    WideIoIndex dest_if_marker{};
    AssetLoader loader = AssetLoader::GenericFileTx;

    std::uint64_t exact_size = 0;
};

inline constexpr std::size_t kMaxBootBuffers = 3;

inline constexpr std::size_t kMaxStartAssets = 5;

inline constexpr svc::SearchDir kCoreHomeDir[] = {svc::SearchDir::CoreDir};

consteval std::array<BootAsset, 5> stock_start_chain(
    std::string_view subdir,
    AssetAnchor anchor = AssetAnchor::Search
) {
    const auto row = [subdir, anchor](std::string_view name, std::uint16_t dest) consteval {
        return BootAsset{
            .name = name,
            .where = kCoreHomeDir,
            .anchor = anchor,
            .subdir = subdir,
            .dest = WideIoIndex{dest}
        };
    };
    return {
        {row("boot0.rom", 0x00),
         row("boot1.rom", 0x40),
         row("boot2.rom", 0x80),
         row("boot3.rom", 0xC0),
         row("boot.rom", 0x00)}
    };
}

inline constexpr auto kCoreHomeChain = stock_start_chain({}, AssetAnchor::CoreHome);
static_assert(kCoreHomeChain.size() <= kMaxStartAssets);

[[nodiscard]] constexpr bool start_row_sends(const BootAsset& row, bool index0_taken) noexcept {
    return !(index0_taken && row.dest.v == 0);
}

}
