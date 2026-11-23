// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <span>

#include "infra/byte_count.h"
#include "infra/error.h"
#include "infra/prelude.h"
#include "proto/track_shape.h"

namespace mister::svc {

class ITrackImage {
public:
    virtual ~ITrackImage() = default;

    [[nodiscard]] virtual proto::TrackShape track_shape() const noexcept = 0;

    [[nodiscard]] virtual Ex<infra::ByteCount> read_track(
        const proto::TrackShape::Ask& ask,
        std::span<std::byte> dst
    ) = 0;

    [[nodiscard]] virtual Ex<void> write_track(
        const proto::TrackShape::Ask& ask,
        std::span<const std::byte> src
    ) = 0;

protected:
    ITrackImage() = default;
    ITrackImage(const ITrackImage&) = default;
    ITrackImage& operator=(const ITrackImage&) = default;
};

}
