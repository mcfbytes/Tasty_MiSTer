// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "infra/seat.h"
#include "svc/types.h"
#include "ui/blob_word.h"

namespace mister::ui {

class JoyMapBlob {
    TASTY_SEAT_EXEMPT(component);

public:
    static constexpr std::size_t kWords = 32;

    static constexpr BlobWord kCntOk{21};
    static constexpr BlobWord kCntEsc{22};
    static constexpr BlobWord kMenuFuncWord{23};

    static constexpr BlobWord kSegment{12};

    [[nodiscard]] svc::EvdevCode code(BlobWord word) const noexcept;

    void store(BlobWord word, svc::EvdevCode code) noexcept;

    [[nodiscard]] bool duplicate_in(BlobWord word, svc::EvdevCode code) const noexcept;

    [[nodiscard]] std::span<const std::uint32_t> words() const noexcept { return blob_; }

private:
    std::array<std::uint32_t, kWords> blob_{};
};

}
