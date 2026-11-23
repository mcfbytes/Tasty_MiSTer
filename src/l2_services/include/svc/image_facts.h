// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <span>
#include <string_view>

#include "infra/error.h"
#include "infra/prelude.h"
#include "proto/types.h"
#include "svc/file.h"

namespace mister::svc {

inline constexpr proto::FileSize kAdmissionProbeBytes{128};

struct ImageFacts {

    std::array<std::byte, kAdmissionProbeBytes.file_word<std::size_t>()> head{};
    proto::FileSize size{};
    std::string_view ext{};
    bool archive = false;

    [[nodiscard]] std::span<const std::byte> file_head() const noexcept {
        return std::span<const std::byte>{head}.first(
            size < kAdmissionProbeBytes ? size.file_word<std::size_t>() : head.size()
        );
    }

    [[nodiscard]] static Ex<ImageFacts> read(
        IFile& file,
        std::string_view path,
        proto::FileSize size
    );
};

}
