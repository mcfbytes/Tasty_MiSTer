// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "cores/nvram_save.h"
#include "hal/types.h"
#include "proto/save_act.h"

namespace mister::cores {

class ISaveServant {
public:
    virtual ~ISaveServant() = default;

    [[nodiscard]] virtual proto::SaveAct start(
        const mra::NvramSave& image,
        std::string_view rel,
        hal::Width word
    ) noexcept = 0;

    [[nodiscard]] virtual proto::SaveAct resume(
        std::span<const std::uint8_t, proto::kSavePieceBytes> bytes
    ) noexcept = 0;

protected:
    ISaveServant() = default;
    ISaveServant(const ISaveServant&) = default;
    ISaveServant& operator=(const ISaveServant&) = default;
};

}
