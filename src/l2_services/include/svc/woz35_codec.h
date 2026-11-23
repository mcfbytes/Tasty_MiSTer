// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <memory>
#include <span>
#include <vector>

#include "infra/error.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "proto/types.h"
#include "svc/disk_image.h"

namespace mister::svc {

class Woz35Codec final : public IDiskImage {
    TASTY_SEAT_RESIDENT(Io);

public:
    [[nodiscard]] static Ex<std::unique_ptr<IDiskImage>> open(
        std::unique_ptr<IDiskImage> source,
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
    Woz35Codec(std::unique_ptr<IDiskImage> source, bool writes_back, std::vector<std::byte> woz);
    [[nodiscard]] Ex<void> write_act_(proto::FileOffset at, std::span<const std::byte> act);

    std::unique_ptr<IDiskImage> source_;
    bool writes_back_;
    std::vector<std::byte> woz_;
    std::vector<std::byte> blocks_;
};

}
