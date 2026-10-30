// SPDX-License-Identifier: GPL-3.0-or-later
#include "proto/spi_status_decoder.h"

#include "hal/selected.h"
#include "proto/uio_opcodes.h"

namespace mister::proto {

void SpiStatusDecoder::service() noexcept {

    if (!session_->flush_owed()) {
        ++flush_refusals_;
        return;
    }
    std::uint8_t nibble = 0;
    {
        hal::Selected cs(*link_, hal::ChipSelect::Io);
        const auto head = link_->transfer(word(UioOp::GetStatus));
        if (!head)
            return;
        const auto stchg = static_cast<std::uint8_t>(head->v & 0xFFu);
        if ((stchg & 0xF0u) != 0xA0u || (stchg & 0x0Fu) == last_)
            return;
        nibble = static_cast<std::uint8_t>(stchg & 0x0Fu);
        for (unsigned i = 0; i < StatusWord::kWords; ++i) {
            const auto w = link_->transfer(hal::SpiWord{0});
            if (!w)
                return;
            words_.words[i] = w->v;
        }
    }
    last_ = nibble;
    if (!session_->adopt_from_core(words_))
        ++flush_refusals_;
    published_ = session_->status().value();
    cell_->publish(published_);
}

}
