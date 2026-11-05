// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string_view>

#include "cores/stream_load_end.h"
#include "infra/fixed_str.h"
#include "infra/seat.h"
#include "proto/types.h"
#include "svc/types.h"

namespace mister::cores {

class StreamTxRecord {
    TASTY_SEAT_RESIDENT(RT);

public:
    void set_ext(std::string_view ext) noexcept { (void)ext_.assign(ext); }

    [[nodiscard]] std::string_view ext() const noexcept { return ext_.view(); }

    void opened() noexcept {
        bytes_ = {};
        crc_ = {};
    }

    void closed(const StreamLoadEnd& end) noexcept {
        if (!end.ok)
            return;
        bytes_ = end.bytes;
        crc_ = end.crc;
    }

    [[nodiscard]] std::uint64_t bytes() const noexcept { return bytes_.v; }

    [[nodiscard]] svc::Crc32 crc() const noexcept { return crc_; }

private:
    FixedStr<8, StrFit::Clip> ext_{};
    proto::FileSize bytes_{};
    svc::Crc32 crc_{};
};

}
