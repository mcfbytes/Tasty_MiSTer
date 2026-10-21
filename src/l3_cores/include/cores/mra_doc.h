// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "cores/cheat_geometry.h"
#include "cores/mra_cheat.h"
#include "cores/rom.h"
#include "cores/switch_table.h"
#include "proto/rotation_dir.h"
#include "svc/vfs.h"

namespace mister::cores::mra {

inline constexpr std::uint64_t kMraDocMax = 16u * 1024u * 1024u;

[[nodiscard]] std::vector<std::uint8_t> hex_bytes(std::string_view text);

[[nodiscard]] std::string_view arcade_root_of(std::string_view mra_path) noexcept;

[[nodiscard]] std::string_view mame_root_of(
    const svc::Vfs& vfs,
    std::string_view manifest_rel
) noexcept;

[[nodiscard]] std::string sibling_name(std::string_view mra_path, std::string_view ext);

struct NvramDecl {
    int index = 0;
    int size = 0;
    std::size_t after = 0;
};

struct MraDoc {
    void parse(std::string_view xml);

    std::vector<Rom> roms;
    SwitchTable switches;
    std::string setname;
    bool setname_same_dir = false;
    bool vertical = false;
    proto::RotationDir rotation = proto::RotationDir::None;
    std::string buttons_names;
    std::string buttons_default;
    int nvram_index = 0;
    int nvram_size = 0;

    std::vector<NvramDecl> nvrams;

    std::vector<MraCheat> cheats;
    CheatGeometry cheat_geometry{};
    std::uint32_t cheats_dropped = 0;
};

}
