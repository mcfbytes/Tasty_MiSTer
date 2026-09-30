// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "infra/error.h"
#include "proto/types.h"

namespace mister::svc {

using proto::FileSize;

enum class FileKind : std::uint8_t {
    Real,
    BlockDev,
    Shm,
    Zip,
    Memory,
};

enum class OpenMode : std::uint8_t { Read, ReadWhole, ReadWrite, Create, Truncate, Sync };

[[nodiscard]] constexpr bool opens_read_only(OpenMode m) noexcept {
    return m == OpenMode::Read || m == OpenMode::ReadWhole;
}

class IFile {
public:
    virtual ~IFile() = default;

    virtual Ex<std::size_t> read_at(std::uint64_t off, std::span<std::byte> dst) = 0;
    virtual Ex<std::size_t> write_at(std::uint64_t off, std::span<const std::byte> src) = 0;

    virtual Ex<FileSize> size() const = 0;
    virtual Ex<void> flush() = 0;
    virtual FileKind kind() const noexcept = 0;

    enum class Access : std::uint8_t { Sequential, WillNeed };
    [[nodiscard]] virtual Ex<void> advise(Access, std::uint64_t off, std::uint64_t len) {
        (void)off;
        (void)len;
        return {};
    }

protected:
    IFile() = default;
    IFile(const IFile&) = default;
    IFile& operator=(const IFile&) = default;
};

}  // namespace mister::svc
