// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string_view>

#include "cores/mailbox_types.h"
#include "cores/minimig/amiga_date.h"
#include "cores/minimig/share_cursor.h"
#include "cores/minimig/share_key.h"
#include "cores/minimig/share_packet.h"
#include "cores/minimig/share_seek_to.h"
#include "cores/minimig/share_wire.h"
#include "infra/affine.h"
#include "infra/byte_count.h"
#include "infra/byte_offset.h"
#include "infra/endian.h"
#include "infra/span_cut.h"
#include "proto/types.h"

namespace mister::cores::minimig {

struct ShareWord {

    static constexpr infra::ByteCount kDosBlock{512};

    std::uint32_t v = 0;

    [[nodiscard]] static constexpr ShareWord load(
        std::span<const std::byte> packet,
        infra::ByteOffset at
    ) noexcept {
        if (!infra::affine::fits(at, kWord, infra::ByteCount{packet.size()}))
            return ShareWord{};
        return ShareWord{infra::load_be<std::uint32_t>(
            infra::affine::from(packet, at).first<sizeof(std::uint32_t)>()
        )};
    }

    [[nodiscard]] static constexpr ShareWord of(ShareLockKey key) noexcept {
        return ShareWord{key.v};
    }

    [[nodiscard]] static constexpr ShareWord of(ShareFileKey key) noexcept {
        return ShareWord{key.v};
    }

    [[nodiscard]] static constexpr ShareWord of(MailboxLen len) noexcept {
        return ShareWord{len.v};
    }

    [[nodiscard]] static constexpr ShareWord of(ShareCursor cursor) noexcept {
        return ShareWord{static_cast<std::uint32_t>(cursor.v)};
    }

    [[nodiscard]] static constexpr ShareWord of(ShareEntry entry) noexcept {
        return ShareWord{static_cast<std::uint32_t>(entry)};
    }

    [[nodiscard]] static constexpr ShareWord of(ShareError error) noexcept {
        return ShareWord{static_cast<std::uint32_t>(error)};
    }

    [[nodiscard]] static constexpr ShareWord of(proto::FileOffset at) noexcept {
        return ShareWord{static_cast<std::uint32_t>(at.seek_offset())};
    }

    [[nodiscard]] static constexpr ShareWord of(AmigaDate::Days days) noexcept {
        return ShareWord{static_cast<std::uint32_t>(days.count())};
    }

    [[nodiscard]] static constexpr ShareWord of(AmigaDate::Minutes minutes) noexcept {
        return ShareWord{static_cast<std::uint32_t>(minutes.count())};
    }

    [[nodiscard]] static constexpr ShareWord of(AmigaDate::Ticks ticks) noexcept {
        return ShareWord{static_cast<std::uint32_t>(ticks.count())};
    }

    [[nodiscard]] static constexpr ShareWord of_blocks(proto::FileSize bytes) noexcept {
        return ShareWord{static_cast<std::uint32_t>(bytes / proto::FileSize::of(kDosBlock))};
    }

    [[nodiscard]] static constexpr ShareWord of_held(proto::FileSize bytes) noexcept {
        return ShareWord{static_cast<std::uint32_t>(
            std::min<std::uint64_t>(bytes.v, std::numeric_limits<std::uint32_t>::max())
        )};
    }

    [[nodiscard]] static constexpr ShareWord verdict(bool ok) noexcept {
        return ShareWord{ok ? 1u : 0u};
    }

    [[nodiscard]] static std::byte bcpl_length(std::string_view name) noexcept {
        constexpr std::uint8_t kLowByte = 0xFF;
        return static_cast<std::byte>(name.size() & kLowByte);
    }

    [[nodiscard]] constexpr ShareAction action() const noexcept {
        return static_cast<ShareAction>(static_cast<std::int32_t>(v));
    }

    [[nodiscard]] constexpr ShareLockKey lock_key() const noexcept { return ShareLockKey{v}; }

    [[nodiscard]] constexpr ShareFileKey file_key() const noexcept { return ShareFileKey{v}; }

    [[nodiscard]] constexpr infra::ByteCount length() const noexcept { return infra::ByteCount{v}; }

    [[nodiscard]] constexpr ShareCursor cursor() const noexcept {
        return ShareCursor{static_cast<std::int32_t>(v)};
    }

    [[nodiscard]] constexpr ShareSeekTo seek_to() const noexcept {
        return ShareSeekTo{static_cast<std::int32_t>(v)};
    }

    [[nodiscard]] constexpr ShareSeek seek_from() const noexcept {
        return static_cast<ShareSeek>(static_cast<std::int32_t>(v));
    }

    [[nodiscard]] constexpr std::optional<infra::ByteOffset> terminator_in(
        std::span<const std::byte> packet
    ) const noexcept {
        return v < packet.size() ? std::optional<infra::ByteOffset>{infra::ByteOffset{v}}
                                 : std::nullopt;
    }

    constexpr void store(std::span<std::byte> out, infra::ByteOffset at) const noexcept {
        if (!infra::affine::fits(at, kWord, infra::ByteCount{out.size()}))
            return;
        infra::store_be<std::uint32_t>(
            infra::affine::from(out, at).first<sizeof(std::uint32_t)>(),
            v
        );
    }

    friend constexpr bool operator==(ShareWord, ShareWord) = default;
};

}
