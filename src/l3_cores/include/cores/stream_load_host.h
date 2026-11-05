// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "cores/stream_load.h"
#include "cores/stream_tx_record.h"
#include "infra/error.h"
#include "infra/seat.h"
#include "proto/session_params.h"
#include "proto/types.h"
#include "svc/types.h"

namespace mister::cores {

struct MountedPath;

class StreamLoadHost final : public IStreamLoad {
    TASTY_SEAT_RESIDENT(RT);

public:
    static constexpr std::size_t kChunkBytes = 4096;

    [[nodiscard]] Ex<proto::SessionParams> stream_opening(IoIndex index) override;
    void stream_closed(const StreamLoadEnd& end) noexcept override;

    void set_pending_ext(std::string_view ext) noexcept { tx_.set_ext(ext); }

    [[nodiscard]] Ex<void> mount(IoIndex slot, const MountedPath& p);

    [[nodiscard]] std::uint64_t bytes() const noexcept { return tx_.bytes(); }

    [[nodiscard]] svc::Crc32 crc() const noexcept { return tx_.crc(); }

    [[nodiscard]] std::uint32_t transfers() const noexcept { return transfers_; }

    [[nodiscard]] std::uint32_t chunks() const noexcept { return chunks_; }

    [[nodiscard]] std::uint32_t mount_refusals() const noexcept { return mount_refusals_; }

    [[nodiscard]] std::uint16_t last_index() const noexcept { return last_index_; }

private:
    StreamTxRecord tx_;
    std::uint32_t transfers_ = 0;
    std::uint32_t chunks_ = 0;
    std::uint32_t mount_refusals_ = 0;
    std::uint16_t last_index_ = 0;
};

}
