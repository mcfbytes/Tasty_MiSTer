// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "cores/romset_config.h"
#include "cores/romset_file.h"

namespace mister::cores::neogeo {

[[nodiscard]] std::string sidecar_altname(std::string_view xml);

[[nodiscard]] bool has_name(std::string_view nameset, std::string_view name);

[[nodiscard]] std::string get_romset(std::string_view path);

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
