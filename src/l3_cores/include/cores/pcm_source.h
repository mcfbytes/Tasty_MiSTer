// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "cores/pcm_extent.h"
#include "infra/error.h"
#include "svc/types.h"

namespace mister::cores {

class IPcmSource {
public:
    virtual ~IPcmSource() = default;

    [[nodiscard]] virtual Ex<PcmExtent> open(svc::TrackNumber track) = 0;

    [[nodiscard]] virtual Ex<std::span<std::byte>> read_at(
        std::uint64_t off,
        std::span<std::byte> dst
    ) = 0;

    virtual void close() noexcept = 0;

protected:
    IPcmSource() = default;
    IPcmSource(const IPcmSource&) = default;
    IPcmSource& operator=(const IPcmSource&) = default;
};

}
