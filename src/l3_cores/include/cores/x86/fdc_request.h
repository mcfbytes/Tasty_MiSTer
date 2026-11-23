// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>

#include "cores/x86/fdc_sector_word.h"
#include "cores/x86/x86_bus_poll.h"
#include "infra/byte_view.h"
#include "proto/types.h"
#include "proto/user_io/poll_frame.h"

namespace mister::cores::x86 {

struct FdcRequest {
    proto::DiskUnit drive{};
    proto::Lba lba{};
    proto::BlockCount count{};
    bool read = false;

    [[nodiscard]] static constexpr std::optional<FdcRequest> decode(
        const proto::user_io::PollFrame& block,
        proto::ReplyIndex params_at
    ) noexcept {
        const proto::ReplyWord req = block.reply(proto::ReplyIndex{});
        if (!req.intersects(kFdcRequest))
            return std::nullopt;
        const proto::ReplyIndex count_at = params_at + proto::ReplyCount{1};
        if (!infra::affine::in_bounds(block.kept, count_at))
            return std::nullopt;
        const FdcSectorWord sector = FdcSectorWord::of(block.replies[params_at]);
        return FdcRequest{
            .drive = sector.drive(),
            .lba = sector.lba(),
            .count = proto::BlockCount::of(block.replies[count_at]),
            .read = req.intersects(kFdcRead)
        };
    }

    friend constexpr bool operator==(const FdcRequest&, const FdcRequest&) = default;
};

}
