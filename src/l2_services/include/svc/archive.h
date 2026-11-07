// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>

#include "infra/error.h"
#include "svc/file.h"
#include "svc/types.h"

namespace mister::svc {

enum class ZipMethod : std::uint16_t { Stored = 0, Deflate = 8 };
static_assert(sizeof(ZipMethod) == sizeof(std::uint16_t), "the method field is a u16");

[[nodiscard]] constexpr std::optional<ZipMethod> method_of(std::uint16_t raw) noexcept {
    if (raw == static_cast<std::uint16_t>(ZipMethod::Stored))
        return ZipMethod::Stored;
    if (raw == static_cast<std::uint16_t>(ZipMethod::Deflate))
        return ZipMethod::Deflate;
    return std::nullopt;
}

static_assert(method_of(0) == ZipMethod::Stored);
static_assert(method_of(8) == ZipMethod::Deflate);
static_assert(!method_of(12).has_value(), "bzip2 is not handled by this reader");

struct ArchiveEntry {
    std::string name;
    std::uint64_t size = 0;
    std::uint64_t packed = 0;
    Crc32 crc{};
    std::uint16_t method = 0;
    bool encrypted = false;

    bool plausible = true;
};

class IArchive {
public:
    virtual ~IArchive() = default;

    [[nodiscard]] virtual std::span<const ArchiveEntry> entries() const noexcept = 0;

    [[nodiscard]] virtual Ex<std::unique_ptr<IFile>> open(std::size_t index) const = 0;

protected:
    IArchive() = default;
    IArchive(const IArchive&) = default;
    IArchive& operator=(const IArchive&) = default;
};

}
