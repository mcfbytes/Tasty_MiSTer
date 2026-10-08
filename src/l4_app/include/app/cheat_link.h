// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string_view>

#include "app/cheat_apply.h"
#include "app/cheat_blob_cell.h"
#include "app/tx_digest_cell.h"
#include "infra/seat.h"

namespace mister::app {

class CheatLink final : public ICheatApply {
    TASTY_SEAT_RESIDENT(RT);

public:
    explicit CheatLink(const CheatBlobCell& blob) noexcept : blob_(blob) {}
    CheatLink(const CheatLink&) = delete;
    CheatLink& operator=(const CheatLink&) = delete;

    void content_loaded(cores::Core* core, TxDigest::Kind kind, std::string_view path,
                        std::uint32_t crc, bool same_game) noexcept override;
    [[nodiscard]] bool apply_cheats_counted(const CheatCall& call) noexcept override;

    [[nodiscard]] const TxDigestCell& tx_digest_cell() const noexcept { return digest_cell_; }
    [[nodiscard]] std::uint32_t applies() const noexcept { return applies_; }
    [[nodiscard]] std::uint32_t refusals() const noexcept { return refusals_; }

private:
    const xthread::Telemetry<CheatBlob, SeatTag::Ui>& blob_;

    TxDigest digest_scratch_{};
    TxDigestCell digest_cell_{};
    CheatBlobCell::Reader blob_reader_{};
    CheatBlob blob_scratch_{};
    std::uint32_t applies_ = 0;
    std::uint32_t refusals_ = 0;
};

}  // namespace mister::app
