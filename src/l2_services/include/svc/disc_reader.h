// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "infra/error.h"
#include "proto/types.h"

namespace mister::svc {

enum class DiscForm : std::uint8_t { UserData, RawFrame, Subcode, FullFrame };

class IDiscReader {
public:
    virtual ~IDiscReader() = default;
    IDiscReader(const IDiscReader&) = delete;
    IDiscReader& operator=(const IDiscReader&) = delete;
    IDiscReader(IDiscReader&&) = delete;
    IDiscReader& operator=(IDiscReader&&) = delete;

    [[nodiscard]] virtual Ex<std::size_t> read_form(DiscForm form, proto::Lba lba,
                                                    std::span<std::byte> dst) = 0;

protected:
    IDiscReader() = default;
};

}  // namespace mister::svc
