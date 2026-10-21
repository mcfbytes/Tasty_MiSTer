// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "svc/file.h"
#include "svc/types.h"
#include "infra/seat.h"

namespace mister::svc {

class MemoryMounts {
    TASTY_SEAT_EXEMPT(const_shared);

public:
    void mount(std::string key, std::span<const std::byte> blob, Crc32 crc);

    [[nodiscard]] bool mounted(std::string_view key) const noexcept;

    [[nodiscard]] std::unique_ptr<IFile> open(std::string_view key) const;

    [[nodiscard]] std::unique_ptr<IFile> open_by_crc_under(
        std::string_view prefix,
        Crc32 crc
    ) const;

    [[nodiscard]] std::uint32_t sequential_hints(std::string_view key) const noexcept;

private:
    struct Blob {
        std::string path;
        std::span<const std::byte> blob;
        Crc32 crc{};
        std::shared_ptr<std::atomic<std::uint32_t>> hints;
    };

    [[nodiscard]] const Blob* find_(std::string_view key) const noexcept;

    std::vector<Blob> blobs_;
};

}
