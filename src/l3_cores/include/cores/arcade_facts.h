// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "cores/mra_doc.h"
#include "cores/mra_nvram_decl.h"
#include "cores/nvram_save.h"
#include "infra/error.h"
#include "proto/types.h"
#include "svc/vfs.h"

namespace mister::cores {

inline constexpr std::uint64_t kNvramCap = 1024u * 1024u;

inline constexpr proto::WideIoIndex kDipIndex{254};

struct ArcadeFacts {
    mra::SwitchTable switches{};
    std::string dip_name;
    std::string nvm_name;

    std::optional<mra::NvramSave> nvram{};
    mra::DipWord dip_cur{};
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

[[nodiscard]] Ex<ArcadeDocument> read_arcade_document(
    std::string_view text,
    std::string_view path,
    const svc::Vfs& vfs
);

}
