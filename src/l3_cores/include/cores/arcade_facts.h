// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "cores/mra_cheat_records.h"
#include "cores/mra_doc.h"
#include "infra/error.h"
#include "svc/vfs.h"

namespace mister::cores {

inline constexpr std::uint64_t kNvramCap = 1024u * 1024u;

inline constexpr std::uint8_t kDipIndex = 254;

struct ArcadeFacts {
    mra::SwitchTable switches{};
    mra::MraCheatRecords cheats{};
    std::uint32_t cheats_dropped = 0;
    std::string btn_names;
    std::string btn_defaults;
    std::string dip_name;
    std::string nvm_name;
    int nvram_index = 0;
    int nvram_size = 0;
    std::uint64_t dip_cur = 0;
    std::string error;
};

struct ArcadeDocument {
    std::vector<mra::Rom> roms;
    std::string rbf;

    std::vector<mra::NvramDecl> nvrams;
    ArcadeFacts facts;
};

[[nodiscard]] Ex<std::string> manifest_document(
    std::string_view text,
    std::string_view path,
    const svc::Vfs& vfs
);

[[nodiscard]] ArcadeDocument read_arcade_document(
    std::string_view text,
    std::string_view path,
    const svc::Vfs& vfs
);

[[nodiscard]] bool nvram_declared(int index, int size) noexcept;

[[nodiscard]] std::array<std::uint8_t, 8> dip_pack(std::uint64_t dip) noexcept;

}
