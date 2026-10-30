// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/error.h"
#include "hal/selected.h"
#include "hal/spi_transport.h"
#include "proto/uio_opcodes.h"

namespace mister::proto {

struct SaveRequestWord {
    std::uint16_t v = 0;

    [[nodiscard]] constexpr bool requested() const noexcept { return v != 0; }

    [[nodiscard]] constexpr std::uint8_t slot() const noexcept {
        return static_cast<std::uint8_t>(v >> 8);
    }

    friend constexpr bool operator==(SaveRequestWord, SaveRequestWord) = default;
};

[[nodiscard]] inline Ex<SaveRequestWord> read_save_request(hal::ISpiTransport& link) {
    hal::Selected cs(link, hal::ChipSelect::Io);
    auto r = link.transfer(word(UioOp::ChkUpload));
    if (!r)
        return std::unexpected(r.error());
    return SaveRequestWord{r->v};
}

}
