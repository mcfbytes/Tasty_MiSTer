// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>
#include <string_view>

#include "cores/nvram_save.h"
#include "cores/save_home.h"
#include "cores/save_shape.h"

namespace mister::cores {

class ISaveUpload {
public:
    virtual ~ISaveUpload() = default;

    [[nodiscard]] virtual std::string_view save_file_name() const noexcept = 0;

    [[nodiscard]] virtual std::optional<mra::NvramSave> save_image() const noexcept = 0;

    [[nodiscard]] virtual SaveHome save_home() const noexcept { return SaveHome::ConfigNvram; }

    [[nodiscard]] virtual bool saved_with_settings() const noexcept { return true; }

    [[nodiscard]] virtual SaveShape save_shape() const noexcept { return {}; }

protected:
    ISaveUpload() = default;
    ISaveUpload(const ISaveUpload&) = default;
    ISaveUpload& operator=(const ISaveUpload&) = default;
};

}
