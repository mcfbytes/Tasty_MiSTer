// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>

#include "infra/error.h"
#include "proto/types.h"
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

struct MemberOrdinal {
    std::uint64_t v = 0;

    template <std::integral Word>
    constexpr void stamp_into(Word& slot) const noexcept {
        slot = static_cast<Word>(v);
    }

    friend constexpr bool operator==(MemberOrdinal, MemberOrdinal) = default;
};

struct ArchiveLocator {
    proto::FileSize directory_offset{};
    MemberOrdinal ordinal{};

    [[nodiscard]] constexpr std::array<std::uint64_t, 2> to_unz() const noexcept {
        std::array<std::uint64_t, 2> words{};
        directory_offset.stamp_into(words[0]);
        ordinal.stamp_into(words[1]);
        return words;
    }
};

struct ArchiveEntry {
    std::string name;
    std::uint64_t size = 0;
    std::uint64_t packed = 0;
    Crc32 crc{};
    std::uint16_t method = 0;
    bool encrypted = false;

    bool plausible = true;

    ArchiveLocator at{};
};

class IArchive {
public:
    virtual ~IArchive() = default;

    [[nodiscard]] virtual std::span<const ArchiveEntry> entries() const noexcept = 0;

    [[nodiscard]] virtual Ex<std::unique_ptr<IFile>> open(const ArchiveEntry& entry) const = 0;

protected:
    IArchive() = default;
    IArchive(const IArchive&) = default;
    IArchive& operator=(const IArchive&) = default;
};

}
