// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <optional>

#include "cores/atari8/sio_partition.h"
#include "infra/byte_count.h"
#include "svc/sector_image.h"
#include "svc/sector_types.h"

namespace mister::cores::atari8 {

class AptTable {
public:
    [[nodiscard]] static std::optional<AptTable> find(svc::ISectorImage& image);

    [[nodiscard]] bool plain() const noexcept;

    [[nodiscard]] SioUnit boot() const noexcept;

    [[nodiscard]] std::optional<SioPartition> entry(SioUnit unit, bool read_only) const;

    static constexpr infra::ByteCount kHalfBytes{256};

private:
    std::array<std::byte, kHalfBytes.v> half_{};
};

}
