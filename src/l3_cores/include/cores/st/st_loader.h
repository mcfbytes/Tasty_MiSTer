// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "cores/file_tx.h"
#include "cores/load_ask.h"
#include "cores/load_plan.h"
#include "cores/loader.h"
#include "cores/wire_chunk.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "proto/status_word.h"
#include "proto/types.h"

namespace mister::cores::st {

class StLoader final : public ILoader {
    TASTY_SEAT_RESIDENT(Main);

public:
    static constexpr proto::WideIoIndex kTosLargeIndex{0};
    static constexpr proto::WideIoIndex kTosSmallIndex{1};
    static constexpr proto::WideIoIndex kCartIndex{2};

    static constexpr proto::FileSize kTosLargeBytes{256u * 1024u};
    static constexpr proto::FileSize kTosSmallBytes{192u * 1024u};

    static constexpr proto::FileSize kCartBytes{128u * 1024u};
    static constexpr proto::FileSize kCartHeaderBytes{4u};
    static constexpr proto::FileSize kCartBufferBytes = kCartBytes + kCartHeaderBytes;

    static constexpr WireChunk kTosChunk = WireChunk::of(kFileTxChunkBytes);

    static constexpr proto::FioWordCount kCartPieceWords{1024};

    static constexpr std::uint8_t kBlankFill = 0xFF;

    [[nodiscard]] Ex<LoadPlan> plan(const LoadAsk& ask, const proto::StatusWord& status) override;

    [[nodiscard]] Ex<void> finish(infra::OptRef<ICoreWindow>) override {
        TASTY_SEAT_BODY(StLoader);
        return {};
    }

    [[nodiscard]] std::span<const std::uint8_t> facts() const noexcept override {
        TASTY_SEAT_BODY(StLoader);
        return {};
    }

private:
    [[nodiscard]] Ex<LoadPlan> tos_plan_(const LoadAsk& ask);
    [[nodiscard]] Ex<LoadPlan> cart_plan_(const LoadAsk& ask);
};

}
