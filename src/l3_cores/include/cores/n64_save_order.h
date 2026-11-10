// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <type_traits>

#include "cores/n64_cpak.h"
#include "cores/nvram_store.h"
#include "cores/types.h"
#include "proto/types.h"

namespace mister::cores::n64 {

enum class CreateImage : std::uint8_t { Never, Blank, ControllerPak };

struct SaveOrder {

    [[nodiscard]] static constexpr bool create_fits(
        CreateImage image,
        proto::FileSize length
    ) noexcept {
        switch (image) {
            case CreateImage::Never:
                return length.empty();
            case CreateImage::Blank:
                return !length.empty();
            case CreateImage::ControllerPak:
                return length == proto::FileSize{cpak::kImageBytes};
        }
        return false;
    }

    SavePath path{};

    std::uint32_t offset = 0;

    std::uint32_t bytes = 0;
    CreateImage create = CreateImage::Never;
    SaveRow row{};
};

static_assert(std::is_trivially_copyable_v<SaveOrder>);

static_assert(sizeof(SaveOrder) == 272 && alignof(SaveOrder) == 4);

}
