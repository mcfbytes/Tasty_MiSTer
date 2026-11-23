// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/sidecar_send.h"

#include <utility>

#include "cores/file_tx.h"
#include "cores/wire_chunk.h"
#include "infra/prelude.h"

namespace mister::cores {

SidecarSend::SidecarSend(SidecarPlan&& plan) {
    append(std::move(plan));
}

void SidecarSend::append(SidecarPlan&& more) {
    TASTY_SEAT_BODY(SidecarSend);
    for (SidecarPayload& item : more.items)
        owed_.push_back(std::move(item));
    more.items.clear();
}

SidecarSend::Pass SidecarSend::step(ILadderHost& host) {
    TASTY_SEAT_BODY(SidecarSend);
    while (pieces_ || !owed_.empty()) {
        if (!pieces_) {
            SidecarPayload item = std::move(owed_.front());
            owed_.pop_front();

            auto p = PayloadPieces::of_bytes(
                std::move(item.bytes),
                {},
                PayloadPieces::Frame{.dest = item.dest, .chunk = WireChunk::of(kFileTxChunkBytes)}
            );
            if (!p) {
                failures_.add();
                continue;
            }
            pieces_.emplace(std::move(*p));
        }
        const auto pass = pieces_->step(host);
        if (pass && *pass == PayloadPieces::Pass::Waiting)
            return Pass::Waiting;
        pass ? sent_.add() : failures_.add();
        pieces_.reset();
    }
    return Pass::Done;
}

}
