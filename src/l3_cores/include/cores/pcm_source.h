// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "cores/pcm_extent.h"
#include "infra/error.h"

namespace mister::cores {

class IPcmSource {
public:
    virtual ~IPcmSource() = default;

    [[nodiscard]] virtual Ex<PcmExtent> open(std::uint8_t track) = 0;

    [[nodiscard]] virtual Ex<std::size_t> read_at(std::uint64_t off, std::span<std::byte> dst) = 0;

    virtual void close() noexcept = 0;

protected:
    IPcmSource() = default;
    IPcmSource(const IPcmSource&) = default;
    IPcmSource& operator=(const IPcmSource&) = default;
};

}  // namespace mister::cores
