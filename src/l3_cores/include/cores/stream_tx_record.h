// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string_view>

#include "cores/stream_load_end.h"
#include "infra/fixed_str.h"
#include "infra/seat.h"

namespace mister::cores {

class StreamTxRecord {
    TASTY_SEAT_RESIDENT(RT);

public:
    void set_ext(std::string_view ext) noexcept { (void)ext_.assign(ext); }

    [[nodiscard]] std::string_view ext() const noexcept { return ext_.view(); }

    void opened() noexcept {
        bytes_ = 0;
        crc_ = 0;
    }

    void closed(const StreamLoadEnd& end) noexcept {
        if (!end.ok)
            return;
        bytes_ = end.bytes;
        crc_ = end.crc;
    }

    [[nodiscard]] std::uint64_t bytes() const noexcept { return bytes_; }

    [[nodiscard]] std::uint32_t crc() const noexcept { return crc_; }

private:
    FixedStr<8, StrFit::Clip> ext_{};
    std::uint64_t bytes_ = 0;
    std::uint32_t crc_ = 0;
};

}
