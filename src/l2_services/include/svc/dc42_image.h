// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>

#include "infra/error.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "proto/types.h"
#include "svc/disk_image.h"
#include "svc/twomg_image.h"

namespace mister::svc {

class Dc42Image final : public IDiskImage {
    TASTY_SEAT_RESIDENT(Io);

public:
    struct Header {
        proto::FileSize data{};

        [[nodiscard]] static std::optional<Header> probe(
            std::span<const std::byte> head,
            proto::FileSize file_size
        ) noexcept;
    };

    static constexpr proto::FileOffset kPayload = proto::kFileStart + proto::FileSize{84};

    [[nodiscard]] static Ex<std::unique_ptr<IDiskImage>> open(
        std::unique_ptr<IDiskImage> file,
        PayloadReach reach
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
    Dc42Image(std::unique_ptr<IDiskImage> file, proto::FileSize extent) noexcept;

    std::unique_ptr<IDiskImage> file_;
    proto::FileSize extent_;
};

}
