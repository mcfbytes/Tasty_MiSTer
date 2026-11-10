// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/pce_sense_reply.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace mister::cores {

PceSenseReply::PceSenseReply() noexcept {
    bytes_[kLength] = kLengthValue;
    bytes_[kControl] = kControlValue;
    bytes_[kResponse] = kResponseValue;
    bytes_[kAdditional] = kAdditionalValue;
}

void PceSenseReply::set_key(std::byte key) noexcept {
    bytes_[kKey] = std::to_integer<std::uint8_t>(key);
}

void PceSenseReply::set_asc(std::byte asc) noexcept {
    bytes_[kAsc] = std::to_integer<std::uint8_t>(asc);
}

std::span<const std::uint8_t, PceSenseReply::kBytes> PceSenseReply::bytes() const noexcept {
    return bytes_;
}

}
