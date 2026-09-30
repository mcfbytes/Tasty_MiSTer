// SPDX-License-Identifier: GPL-3.0-or-later
#include "proto/spi_status_decoder.h"

#include "hal/selected.h"

namespace mister::proto {
namespace {

constexpr hal::SpiWord kStatusPoll{0x29};

}

void SpiStatusDecoder::service() noexcept {
    StatusRegister& shadow = session_->status();
    if (shadow.dirty()) {
        if (!session_->flush_status()) {
            ++flush_refusals_;
            return;
        }
    }
    std::uint8_t nibble = 0;
    {
        hal::Selected cs(*link_, hal::ChipSelect::Io);
        const auto head = link_->transfer(kStatusPoll);
        if (!head) return;
        const auto stchg = static_cast<std::uint8_t>(head->v & 0xFFu);
        if ((stchg & 0xF0u) != 0xA0u || (stchg & 0x0Fu) == last_) return;
        nibble = static_cast<std::uint8_t>(stchg & 0x0Fu);
        for (unsigned i = 0; i < StatusRegister::kWords; ++i) {
            const auto w = link_->transfer(hal::SpiWord{0});
            if (!w) return;
            words_[i] = w->v;
        }
    }
    last_ = nibble;
    words_[0] = static_cast<std::uint16_t>(words_[0] & ~1u);
    shadow.adopt(words_);
    if (!session_->flush_status()) ++flush_refusals_;
    published_ = shadow.value();
    cell_->publish(published_);
}

}  // namespace mister::proto
