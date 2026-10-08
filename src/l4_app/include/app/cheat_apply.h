// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string_view>

#include "app/cheat_call.h"
#include "app/tx_digest_cell.h"

namespace mister::cores {
class Core;
}

namespace mister::app {

class ICheatApply {
public:
    virtual ~ICheatApply() = default;

    virtual void content_loaded(cores::Core* core, TxDigest::Kind kind, std::string_view path,
                                std::uint32_t crc, bool same_game) noexcept = 0;

    [[nodiscard]] virtual bool apply_cheats_counted(const CheatCall& call) noexcept = 0;

protected:
    ICheatApply() = default;
    ICheatApply(const ICheatApply&) = default;
    ICheatApply& operator=(const ICheatApply&) = default;
};

}  // namespace mister::app
