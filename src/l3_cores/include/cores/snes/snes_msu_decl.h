// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>

#include "cores/core_window_decl.h"
#include "cores/snes/msu_wire.h"
#include "proto/mailbox_frame_decl.h"

namespace mister::cores::snes {

inline constexpr proto::MailboxFrameDecl kSnesMailboxFrame{
    .poll_opcode = proto::UioOp::CdGet,
    .words = msu::kFrameWords
};

inline constexpr auto kSnesWindows = std::to_array<CoreWindowDecl>({
    {.region = {msu::kTrackOffset, 0u, "snes-msu-data"}, .feeds_pcm = false},
});

}
