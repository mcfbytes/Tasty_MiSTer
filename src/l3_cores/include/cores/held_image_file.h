// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "infra/error.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "proto/types.h"
#include "svc/file.h"

namespace mister::cores {

class HeldImageFile final : public svc::IFile {
    TASTY_SEAT_RESIDENT(Main);

public:
    explicit HeldImageFile(std::vector<std::uint8_t>&& image) noexcept;

    [[nodiscard]] Ex<infra::ByteCount> read_at(
        proto::FileOffset at,
        std::span<std::byte> dst
    ) override;
    [[nodiscard]] Ex<infra::ByteCount> write_at(
        proto::FileOffset at,
        std::span<const std::byte> src
    ) override;
    [[nodiscard]] Ex<proto::FileSize> size() const override;
    [[nodiscard]] Ex<void> flush() override;

private:
    std::vector<std::uint8_t> image_;
};

}
