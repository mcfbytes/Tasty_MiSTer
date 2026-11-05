// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "infra/error.h"
#include "proto/types.h"

namespace mister::svc {

inline constexpr std::size_t kStorageOrderBytes = 2304;

class IStorageBackend {
public:
    virtual ~IStorageBackend() = default;

    [[nodiscard]] virtual Ex<std::size_t> read_at(
        std::uint64_t offset,
        std::span<std::uint8_t> dst
    ) = 0;
    [[nodiscard]] virtual Ex<std::size_t> write_at(
        std::uint64_t offset,
        std::span<const std::uint8_t> src
    ) = 0;

    [[nodiscard]] virtual Ex<proto::FileSize> open_extent(
        proto::SlotIndex slot,
        std::span<const std::byte> order
    ) = 0;

    [[nodiscard]] virtual Ex<proto::FileSize> create(std::span<const std::uint8_t>) {
        return std::unexpected(Error{Errc::not_found, ERR_SITE(), 0});
    }

    [[nodiscard]] virtual Ex<void> flush() { return {}; }

    virtual void note_tag(proto::ImageTag) noexcept {}

    virtual void release(proto::SlotIndex slot) noexcept = 0;

protected:
    IStorageBackend() = default;
    IStorageBackend(const IStorageBackend&) = default;
    IStorageBackend& operator=(const IStorageBackend&) = default;
};

}
