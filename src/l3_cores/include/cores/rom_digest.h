// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <span>

#include "cores/digest_value.h"
#include "cores/md5.h"
#include "infra/seat.h"

namespace mister::cores {

class RomDigest {
    TASTY_SEAT_EXEMPT(component);

public:
    explicit RomDigest(DigestKind kind = DigestKind::Md5) noexcept;
    void update(std::span<const std::uint8_t> data) noexcept;

    [[nodiscard]] DigestValue finish() noexcept;
    [[nodiscard]] DigestKind kind() const noexcept { return kind_; }

private:
    void block_(const std::uint8_t* p) noexcept;
    void sha1_block_(const std::uint8_t* p) noexcept;
    void sha256_block_(const std::uint8_t* p) noexcept;

    DigestKind kind_;
    mra::Md5 md5_{};
    std::array<std::uint32_t, 8> h_{};
    std::uint64_t total_ = 0;
    std::array<std::uint8_t, 64> tail_{};
};

}  // namespace mister::cores
