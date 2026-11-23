// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "cores/mailbox_types.h"
#include "cores/x86/dos_attr.h"
#include "cores/x86/dos_cursor.h"
#include "cores/x86/dos_open_mode.h"
#include "cores/x86/dos_path.h"
#include "cores/x86/dos_share_op.h"
#include "cores/x86/dos_token.h"
#include "cores/x86/x86_share_key.h"
#include "hal/reg_field.h"
#include "infra/byte_count.h"
#include "infra/byte_offset.h"
#include "infra/endian.h"
#include "proto/types.h"

namespace mister::cores::x86 {

struct DosShareRequest {

    enum class Missing : std::uint8_t { Fail, Create };
    enum class Existing : std::uint8_t { Fail, Open, Truncate };

    struct SpOpenAction {
        using Field = hal::RegField<std::uint16_t>;

        static constexpr Field kMissing{4, 0x0F};
        static constexpr Field kExisting{0, 0x0F};
        static constexpr std::uint16_t kCreateIfMissing = 1;
        static constexpr std::uint16_t kOpenExisting = 1;
        static constexpr std::uint16_t kTruncateExisting = 2;

        Missing missing{};
        Existing existing{};

        [[nodiscard]] static constexpr SpOpenAction of(std::span<const std::byte> bytes) noexcept {
            if (bytes.size() < sizeof(std::uint16_t))
                return {};
            const std::uint16_t word = infra::load_le<std::uint16_t>(
                bytes.first<sizeof(std::uint16_t)>()
            );
            const std::uint16_t missing = kMissing.get(word);
            const std::uint16_t existing = kExisting.get(word);
            return SpOpenAction{
                .missing = missing == kCreateIfMissing ? Missing::Create : Missing::Fail,
                .existing = existing == kTruncateExisting ? Existing::Truncate
                    : existing == kOpenExisting           ? Existing::Open
                                                          : Existing::Fail
            };
        }
    };

    struct Displacement {
        std::int32_t v = 0;

        [[nodiscard]] static constexpr Displacement of(std::span<const std::byte> bytes) noexcept {
            if (bytes.size() < sizeof(std::int32_t))
                return {};
            return Displacement{infra::load_le<std::int32_t>(bytes.first<4>())};
        }

        [[nodiscard]] constexpr proto::FileOffset from(proto::FileSize end) const noexcept {
            const std::optional<proto::FileOffset> wanted = (proto::kFileStart + end).seek_by(v);
            return wanted ? *wanted : proto::kFileStart;
        }
    };

    std::optional<DosShareOp> op{};
    MailboxLen length{};
    DosPath path{};
    DosPath target{};
    ShareFileKey file{};
    ShareLockKey lock{};
    DosToken token{};
    proto::FileOffset at{};
    infra::ByteCount count{};
    DosOpenMode mode{};
    DosAttr attr{};
    SpOpenAction action{};
    Displacement displacement{};
    DosCursor cursor{};

    std::optional<infra::ByteOffset> terminator{};

    std::optional<infra::ByteOffset> split{};
};

}
