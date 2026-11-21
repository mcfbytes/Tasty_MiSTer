// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

#include "cores/snes/snes_rom.h"
#include "infra/error.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "proto/types.h"
#include "svc/file.h"

namespace mister::cores::snes::detail {

struct ImageExtent {
    proto::FileSize image_at;
    proto::FileSize payload;
    proto::FileSize padded;
};

class SnesRomFile final : public svc::IFile {
    TASTY_SEAT_RESIDENT(Main);

public:
    SnesRomFile(
        std::unique_ptr<svc::IFile> raw,
        ImageExtent extent,
        std::array<std::uint8_t, kHeaderBlockBytes> header
    );

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
    static constexpr proto::FileOffset kHeaderEnd = proto::kFileStart
        + proto::FileSize{kHeaderBlockBytes};

    std::unique_ptr<svc::IFile> raw_;
    ImageExtent extent_;
    std::array<std::uint8_t, kHeaderBlockBytes> header_{};
};

}
