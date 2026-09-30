// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>
#include <string_view>

#include "infra/error.h"

namespace mister::cores {

class ISaveUpload {
public:
    virtual ~ISaveUpload() = default;

    [[nodiscard]] virtual std::string_view save_file_name() const noexcept = 0;

    [[nodiscard]] virtual Ex<std::span<const std::uint8_t>> pull_save() = 0;

protected:
    ISaveUpload() = default;
    ISaveUpload(const ISaveUpload&) = default;
    ISaveUpload& operator=(const ISaveUpload&) = default;
};

}  // namespace mister::cores
