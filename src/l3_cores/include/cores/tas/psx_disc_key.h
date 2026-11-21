// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "infra/error.h"
#include "infra/prelude.h"
#include "proto/types.h"
#include "svc/disc/track.h"

namespace mister::svc::disc {
class IChdSource;
}

namespace mister::cores::tas {

class PsxDiscKey final {
public:
    static constexpr std::size_t kMaxFiles = 99;

    static constexpr std::size_t kTocPrefixBytes = 4 * (3 + 3 * 100);

    static constexpr std::uint64_t kKeySectors = 26;
    static constexpr proto::BlockSize kSectorSize{svc::disc::kRawFrameBytes};

    [[nodiscard]] static constexpr proto::FileSize key_bytes() noexcept {
        return kSectorSize.bytes_of(proto::BlockCount{kKeySectors});
    }

    static constexpr std::size_t kChdFrameBytes = svc::disc::kFullFrameBytes;
    static constexpr std::uint32_t kChdHunkMax = 1u << 20;

    [[nodiscard]] static Ex<std::vector<std::string>> cue_files(std::string_view cue);

    [[nodiscard]] static Ex<std::vector<std::uint8_t>> toc_prefix(
        std::string_view cue,
        std::span<const proto::FileSize> file_sizes
    );

    [[nodiscard]] static Ex<std::vector<std::uint8_t>> chd_key(svc::disc::IChdSource& chd);

    [[nodiscard]] static Error chd_open_error(const Error& e) noexcept;
};

}
