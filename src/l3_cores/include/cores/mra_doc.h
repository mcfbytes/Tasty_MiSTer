// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "cores/cheat_geometry.h"
#include "cores/mra_cheat.h"
#include "cores/mra_map_word.h"
#include "cores/mra_nvram_decl.h"
#include "cores/nvram_save.h"
#include "cores/rom.h"
#include "cores/switch_table.h"
#include "proto/rotation_dir.h"
#include "proto/types.h"
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

[[nodiscard]] std::optional<proto::WideIoIndex> rom_index_of(std::string_view digits) noexcept;

[[nodiscard]] std::optional<proto::WideIoIndex> nvram_index_of(std::string_view digits) noexcept;

[[nodiscard]] std::optional<proto::FileSize> nvram_size_of(std::string_view digits) noexcept;

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

    std::optional<proto::WideIoIndex> nvram_index{};
    std::optional<proto::FileSize> nvram_size{};

    std::vector<NvramDecl> nvrams;

    std::vector<MraCheat> cheats;
    CheatGeometry cheat_geometry{};
    std::uint32_t cheats_dropped = 0;

    [[nodiscard]] std::optional<NvramSave> nvram_save() const noexcept {
        if (!nvram_index || !nvram_size)
            return std::nullopt;
        return NvramSave{.index = *nvram_index, .size = *nvram_size};
    }
};

}
