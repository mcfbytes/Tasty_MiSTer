// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "infra/error.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "proto/types.h"
#include "svc/disk_image.h"

namespace mister::svc {

inline constexpr proto::FileSize kWoz525ImageBytes{234'496};

class Woz525Codec final : public IDiskImage {
    TASTY_SEAT_RESIDENT(Io);

public:
    enum class SectorOrder : std::uint8_t { Dos, ProDos, Nibble };

    [[nodiscard]] static Ex<std::unique_ptr<IDiskImage>> open(
        std::unique_ptr<IDiskImage> source,
        SectorOrder order,
        bool writes_back
    );

    [[nodiscard]] FileSize size() const noexcept override;
    [[nodiscard]] bool writable() const noexcept override;
    [[nodiscard]] Ex<infra::ByteCount> read_at(
        proto::FileOffset at,
        std::span<std::byte> dst
    ) override;

    [[nodiscard]] Ex<infra::ByteCount> write_at(
        proto::FileOffset at,
        std::span<const std::byte> src
    ) override;

private:
    Woz525Codec(std::unique_ptr<IDiskImage> source, SectorOrder order, bool writes_back);
    [[nodiscard]] Ex<void> write_act_(proto::FileOffset at, std::span<const std::byte> act);

    std::unique_ptr<IDiskImage> source_;
    SectorOrder order_;
    bool writes_back_;
    std::vector<std::byte> woz_;
    std::vector<std::byte> dos_;
};

}
