// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "hal/board_profile.h"
#include "hal/phys_region.h"
#include "hal/spi_words.h"
#include "proto/mailbox_act.h"
#include "proto/mailbox_frame.h"
#include "proto/types.h"
#include "proto/uio_opcodes.h"

namespace mister::cores::snes::msu {

inline constexpr std::uint8_t kCdSet = static_cast<std::uint8_t>(proto::UioOp::CdSet);
inline constexpr std::uint8_t kFrameWords = 4;

inline constexpr std::uint16_t kSetEnable = 1;
inline constexpr std::uint16_t kSetTrackSize = 2;
inline constexpr std::uint16_t kSetDataBase = 3;

enum class Request : std::uint16_t { Next = 0x34, Track = 0x35, Seek = 0x36, Reset = 0xFF };

[[nodiscard]] constexpr Request request_word(hal::SpiWord w) noexcept {
    return w.decoded([](std::uint16_t bits) noexcept { return Request{bits}; });
}

[[nodiscard]] constexpr std::optional<Request> request_of(
    const proto::MailboxFrame& frame
) noexcept {
    const Request request = request_word(frame.w[1]);
    switch (request) {
        case Request::Next:
        case Request::Track:
        case Request::Seek:
        case Request::Reset:
            return request;
    }
    return std::nullopt;
}

inline constexpr proto::BlockSize kSectorSize{1024};
inline constexpr std::uint16_t kAudioIndex = 2;
inline constexpr std::uint8_t kDataIndex = 3;

inline constexpr std::uint32_t kCoreHoleOffset = 0x0200'0000u;
inline constexpr std::uint32_t kCoreHoleBytes = 0x0080'0000u;

inline constexpr std::uint32_t kDataBaseOffset = 0x0300'0000u;

inline constexpr std::uint32_t kTrackOffset = 0x0380'0000u;

inline constexpr std::uint32_t kSaveStateOffset = 0x1F80'0000u;
inline constexpr proto::FileSize kDataBound{0x1C00'0000u};

static_assert(
    kDataBaseOffset >= kCoreHoleOffset && kDataBaseOffset == kTrackOffset - kCoreHoleBytes
);
static_assert(proto::FileSize{kTrackOffset} + kDataBound == proto::FileSize{kSaveStateOffset});

struct TrackCursor {
    std::uint64_t v = 0;
    using affine_count = proto::FileSize;

    template <std::integral Word>
    [[nodiscard]] constexpr Word file_word() const noexcept {
        return static_cast<Word>(v);
    }

    friend constexpr bool operator==(TrackCursor, TrackCursor) = default;
    friend constexpr auto operator<=>(TrackCursor, TrackCursor) = default;
};

[[nodiscard]] constexpr proto::MailboxWords set_words(std::uint64_t v) noexcept {
    const hal::SpiWords<3> words = hal::SpiWords<3>::le(v);
    return {words[0], words[1], words[2]};
}

[[nodiscard]] constexpr proto::MailboxWords set_enable(bool present) noexcept {
    return set_words((std::uint64_t{present ? 1u : 0u} << 15) | kSetEnable);
}

[[nodiscard]] constexpr proto::MailboxWords set_track_size(std::uint32_t size) noexcept {
    return set_words((std::uint64_t{size} << 16) | kSetTrackSize);
}

[[nodiscard]] constexpr proto::MailboxWords set_data_base() noexcept {
    const auto halves = hal::kBoardApertures.front().translate(kDataBaseOffset)->low_halves();
    return {hal::SpiWord{kSetDataBase}, hal::SpiWord{halves[0]}, hal::SpiWord{halves[1]}};
}

}
