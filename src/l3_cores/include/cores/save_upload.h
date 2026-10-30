// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string_view>

#include "cores/save_image.h"

namespace mister::cores {

class ISaveUpload {
public:
    virtual ~ISaveUpload() = default;

    [[nodiscard]] virtual std::string_view save_file_name() const noexcept = 0;

    [[nodiscard]] virtual SaveImage save_image() const noexcept = 0;

protected:
    ISaveUpload() = default;
    ISaveUpload(const ISaveUpload&) = default;
    ISaveUpload& operator=(const ISaveUpload&) = default;
};

}
