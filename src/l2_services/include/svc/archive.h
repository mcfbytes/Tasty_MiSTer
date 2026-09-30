// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>

#include "infra/error.h"
#include "svc/file.h"

namespace mister::svc {

struct ArchiveEntry {
    std::string name;
    std::uint64_t size = 0;
    std::uint64_t packed = 0;
    std::uint32_t crc = 0;
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

}  // namespace mister::svc
