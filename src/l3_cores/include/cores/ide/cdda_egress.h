// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "cores/ide/atapi_drive.h"
#include "infra/seat.h"
#include "infra/byte_count.h"
#include "proto/disk_act.h"
#include "svc/disc/track.h"

namespace mister::cores::ide {

class CddaEgress final {
    TASTY_SEAT_RESIDENT(Io);

public:
    static constexpr infra::ByteCount kPiece{proto::kDiskActBytes};

    static constexpr infra::ByteCount kSampleBytes{sizeof(std::int16_t)};
    static constexpr infra::ByteCount kPairBytes{2 * sizeof(std::int16_t)};
    static_assert(
        svc::disc::kRawFrameBytes % kPairBytes.size() == 0,
        "a frame is whole stereo pairs"
    );
    static_assert(proto::kDiskActBytes % kPairBytes.size() == 0, "a piece is whole stereo pairs");
    static_assert(
        (svc::disc::kRawFrameBytes % proto::kDiskActBytes) % kPairBytes.size() == 0,
        "a frame's last piece is whole stereo pairs"
    );

    CddaEgress() noexcept = default;
    CddaEgress(const CddaEgress&) = delete;
    CddaEgress& operator=(const CddaEgress&) = delete;

    struct Piece {
        std::span<const std::byte> bytes{};
        bool last = false;
    };

    [[nodiscard]] bool stage(AtapiDrive& drive) noexcept;

    [[nodiscard]] std::optional<Piece> next() noexcept;

    [[nodiscard]] bool pending() const noexcept { return !rest_.empty(); }

    void clear() noexcept { rest_ = {}; }

private:
    void level_(AtapiDrive::Volumes volumes) noexcept;

    std::array<std::byte, svc::disc::kRawFrameBytes> frame_{};

    std::span<const std::byte> rest_{};
};

}
