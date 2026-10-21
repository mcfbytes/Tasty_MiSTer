// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "app/interned_file.h"
#include "proto/types.h"

namespace mister::app {

inline constexpr std::size_t kLinkTxSlabBytes = 256;

class ILinkTxReader {
public:
    [[nodiscard]] virtual std::span<const std::uint8_t> bytes(
        proto::TxSlabId id
    ) const noexcept = 0;
    [[nodiscard]] virtual const InternedFile* file(proto::FileId id) const noexcept = 0;

protected:
    constexpr ILinkTxReader() noexcept = default;
    ~ILinkTxReader() = default;
    ILinkTxReader(const ILinkTxReader&) = default;
    ILinkTxReader& operator=(const ILinkTxReader&) = default;
};

}
