// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cstdint>
#include <span>

#include "cores/atari8/atari8_address.h"
#include "cores/atari8/atari8_wire.h"
#include "hal/types.h"
#include "infra/byte_count.h"
#include "proto/mailbox_act.h"

namespace mister::cores::atari8 {

class Atari8Fill {
public:
    enum class Pattern : std::uint8_t { PowerOn, Zeros };

    struct Extent {
        Atari8Address from{};
        Atari8Address end{};
    };

    constexpr Atari8Fill() noexcept = default;

    constexpr Atari8Fill(Extent extent, Pattern pattern) noexcept
        : at_(extent.from),
          end_(extent.end),
          pattern_(pattern) {}

    static const Atari8Fill kComputerRam;
    static const Atari8Fill kConsoleRam;
    static const Atari8Fill kConsoleCart;

    [[nodiscard]] constexpr Atari8Fill cleared() const noexcept {
        return Atari8Fill{{at_, end_}, Pattern::Zeros};
    }

    [[nodiscard]] constexpr bool done() const noexcept { return !(at_ < end_); }

    [[nodiscard]] constexpr Atari8Address at() const noexcept { return at_; }

    [[nodiscard]] constexpr proto::MailboxAct::AddressedDownload block(
        std::span<std::uint8_t, proto::kMailboxActBytes> bytes
    ) const noexcept {
        paint_(bytes);
        return {
            .address = at_.aux(),
            .index = wire::kDmaIndex,
            .len = proto::MailboxActLen::whole()
        };
    }

    [[nodiscard]] constexpr Atari8Fill after_block() const noexcept {
        return Atari8Fill{{at_.next_block(), end_}, pattern_};
    }

private:
    constexpr void paint_(std::span<std::uint8_t, proto::kMailboxActBytes> bytes) const noexcept {
        switch (pattern_) {
            case Pattern::Zeros:
                std::ranges::fill(bytes, std::uint8_t{0});
                return;
            case Pattern::PowerOn:
                break;
        }
        static_assert(
            proto::kMailboxActBytes % sizeof(hal::LinkWord) == 0,
            "an act is whole words"
        );
        for (std::span<std::uint8_t> rest = bytes; !rest.empty();
             rest = rest.subspan(sizeof(hal::LinkWord)))
            kPowerOn.store_le(rest.first<sizeof(hal::LinkWord)>());
    }

    static constexpr infra::ByteCount kComputerRamBytes{0x1'0000};
    static constexpr infra::ByteCount kConsoleRamBytes{0x4000};

    static constexpr infra::ByteCount kConsoleCartFrom{0x4000};
    static constexpr infra::ByteCount kConsoleCartEnd{0x8'4000};

    static constexpr hal::LinkWord kPowerOn{0x00FF};

    Atari8Address at_{};
    Atari8Address end_{};
    Pattern pattern_ = Pattern::Zeros;
};

inline constexpr Atari8Fill Atari8Fill::kComputerRam{
    {Atari8Address::kSdram, Atari8Address::kSdram.after(Atari8Fill::kComputerRamBytes)},
    Pattern::PowerOn
};
inline constexpr Atari8Fill Atari8Fill::kConsoleRam{
    {Atari8Address::kHostMemory, Atari8Address::kHostMemory.after(Atari8Fill::kConsoleRamBytes)},
    Pattern::PowerOn
};
inline constexpr Atari8Fill Atari8Fill::kConsoleCart{
    {Atari8Address::kSdram.after(Atari8Fill::kConsoleCartFrom),
     Atari8Address::kSdram.after(Atari8Fill::kConsoleCartEnd)},
    Pattern::Zeros
};

}
