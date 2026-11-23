// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "proto/types.h"

namespace mister::cores::minimig {

struct CdStatus {
    proto::ReplyWord word{};

    static constexpr proto::ReplyMask kDrives{0x3FC0};
    static constexpr proto::ReplyMask kCdtvRequest{1u << 6};
    static constexpr proto::ReplyMask kAkikoNvramDirty{1u << 7};
    static constexpr proto::ReplyMask kCddaRequest{1u << 8};
    static constexpr proto::ReplyMask kAkikoRxBusy{1u << 9};
    static constexpr proto::ReplyMask kAkikoSectorRequest{1u << 10};
    static constexpr proto::ReplyMask kAkikoRequest{1u << 11};
    static constexpr proto::ReplyMask kCdtvNvramDirty{1u << 12};
    static constexpr proto::ReplyMask kCdtvCardDirty{1u << 13};

    [[nodiscard]] constexpr bool cdtv_request() const noexcept {
        return word.intersects(kCdtvRequest);
    }

    [[nodiscard]] constexpr bool akiko_nvram_dirty() const noexcept {
        return word.intersects(kAkikoNvramDirty);
    }

    [[nodiscard]] constexpr bool cdda_request() const noexcept {
        return word.intersects(kCddaRequest);
    }

    [[nodiscard]] constexpr bool akiko_rx_busy() const noexcept {
        return word.intersects(kAkikoRxBusy);
    }

    [[nodiscard]] constexpr bool akiko_sector_request() const noexcept {
        return word.intersects(kAkikoSectorRequest);
    }

    [[nodiscard]] constexpr bool akiko_request() const noexcept {
        return word.intersects(kAkikoRequest);
    }

    [[nodiscard]] constexpr bool cdtv_nvram_dirty() const noexcept {
        return word.intersects(kCdtvNvramDirty);
    }

    [[nodiscard]] constexpr bool cdtv_card_dirty() const noexcept {
        return word.intersects(kCdtvCardDirty);
    }

    friend constexpr bool operator==(CdStatus, CdStatus) = default;
};

}
