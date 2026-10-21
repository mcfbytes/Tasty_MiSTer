// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "cores/romset_alias.h"
#include "infra/seat.h"

namespace mister::cores::neogeo {

inline constexpr std::size_t kRomsetIndexMax = 1000;

inline constexpr std::uint32_t kVromSize = 16u * 1024u * 1024u;

struct RomsetEntry {
    std::string name;

    std::string altname;
    bool hide = false;
};

struct RomsetIndex {
    TASTY_SEAT_EXEMPT(component);
    std::vector<RomsetEntry> entries;

    void scan(std::string_view xml);

    [[nodiscard]] RomsetAlias lookup(std::string_view key) const;
};

[[nodiscard]] std::string sidecar_altname(std::string_view xml);

[[nodiscard]] bool has_name(std::string_view nameset, std::string_view name);

[[nodiscard]] std::string get_romset(std::string_view path);

enum class FileClass : std::uint8_t { Raw, EightBit, Fix, Spr };

struct RomsetFile {
    std::string name;
    FileClass file_class = FileClass::Raw;
    std::uint8_t index = 0;
    std::uint32_t offset = 0;
    std::uint32_t size = 0;
    std::uint32_t expand = 0;
};

struct RomsetConfig {
    std::uint32_t ms5p = 0;
    std::uint32_t xram = 0;
    std::uint32_t hw_type = 0;
    std::uint32_t cmc = 0;
    std::uint32_t pvc = 0;
    std::uint32_t sma = 0;
    std::uint32_t use_pcm = 1;
    std::uint32_t rom_wait = 0;
    std::uint32_t p_wait = 0;
};

[[nodiscard]] constexpr std::uint32_t config_word(const RomsetConfig& c, bool cd_en) noexcept {
    std::uint32_t w = cd_en ? (1u << 31) : 0u;
    w |= (c.ms5p & 1u) << 17;
    w |= (c.xram & 1u) << 18;
    w |= (c.hw_type & 3u) << 24;
    w |= (c.cmc & 3u) << 26;
    if (c.pvc != 0) {
        w |= 2u << 20;
    } else if (c.sma != 0) {
        w |= ((2u + c.sma) & 7u) << 20;
    }
    w |= (c.use_pcm != 0 ? 1u : 0u) << 23;
    w |= (c.use_pcm == 2 ? 1u : 0u) << 19;
    w |= (c.rom_wait & 1u) << 28;
    w |= (c.p_wait & 3u) << 29;
    return w;
}

struct RomsetPlan {
    bool found = false;
    RomsetConfig config;
    std::vector<RomsetFile> files;

    bool no_files_declared = false;
    std::uint32_t vromb_offset = 0;
    bool vrom_mirror = true;
    bool force_pcm = false;
};

[[nodiscard]] RomsetPlan load_files(std::string_view xml, std::string_view romset);

[[nodiscard]] std::vector<RomsetFile> fallback_files(const RomsetPlan& plan);

}
