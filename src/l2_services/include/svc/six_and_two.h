// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <optional>
#include <span>

#include "proto/types.h"
#include "svc/nib_track.h"

namespace mister::svc {

[[nodiscard]] std::byte disk_byte_of_six(std::byte six) noexcept;
[[nodiscard]] std::optional<std::byte> six_of_disk_byte(std::byte disk) noexcept;

void encode_nib_track(
    NibTrack track,
    std::span<const std::byte> sectors,
    std::span<std::byte> nibbles
) noexcept;

class NibSectorWalk {
public:
    struct Found {
        std::byte track{};
        std::byte sector{};
        DosSectorBytes data{};

        [[nodiscard]] std::optional<proto::FileSize> place_in_track() const noexcept;
    };

    explicit NibSectorWalk(std::span<const std::byte> window) noexcept : window_(window) {}

    [[nodiscard]] std::optional<Found> next() noexcept;

private:
    std::span<const std::byte> window_;
};

}
