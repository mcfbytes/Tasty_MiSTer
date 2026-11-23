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

namespace mister::svc {

enum class PayloadReach : std::uint8_t { Declared, ToFileEnd };

class TwoMgImage final : public IDiskImage {
    TASTY_SEAT_RESIDENT(Io);

public:
    enum class Order : std::uint32_t { Dos = 0, ProDos = 1, Nibble = 2 };

    struct Header {
        Order order{};
        proto::FileOffset payload{};
        proto::FileSize declared{};

        [[nodiscard]] static std::optional<Header> parse(
            std::span<const std::byte> head,
            proto::FileSize file_size
        ) noexcept;
    };

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
    TwoMgImage(
        std::unique_ptr<IDiskImage> file,
        proto::FileOffset payload,
        proto::FileSize extent
    ) noexcept;

    std::unique_ptr<IDiskImage> file_;
    proto::FileOffset payload_;
    proto::FileSize extent_;
};

}
