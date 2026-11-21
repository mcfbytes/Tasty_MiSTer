// SPDX-License-Identifier: GPL-3.0-or-later
#include "proto/link/spi_status_decoder.h"

#include "hal/selected.h"
#include "proto/uio_opcodes.h"

namespace mister::proto::link {

std::optional<StatusChange> SpiStatusSource::take(StatusChange held, StatusWord& words) noexcept {
    TASTY_SEAT_BODY(SpiStatusSource);
    std::optional<StatusChange> change;
    {
        hal::Selected cs(link_, hal::ChipSelect::Io);
        const auto head = link_.transfer(word(UioOp::GetStatus));
        if (!head)
            return std::nullopt;
        change = StatusChange::of_word(*head);
        if (!change || *change == held)
            return std::nullopt;
        for (unsigned i = 0; i < StatusWord::kWords; ++i) {
            const auto w = link_.transfer(hal::SpiWord{0});
            if (!w)
                return std::nullopt;
            words.words[i] = *w;
        }
    }
    return change;
}

}
