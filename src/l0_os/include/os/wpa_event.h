// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <tuple>
#include <type_traits>

#include "infra/fixed_str.h"
#include "infra/message_sum.h"
#include "infra/seat.h"

namespace mister::os {

inline constexpr std::size_t kWpaEventReasonChars = 24;

struct WpaEvent {
    TASTY_SEAT_EXEMPT(component);

    enum class Kind : std::uint8_t {
        ScanResults,
        ScanFailed,
        Connected,
        Disconnected,
        SsidTempDisabled,
        NetworkNotFound,
        AssocReject,
        Terminating,
        RegdomChange,
        Unhandled,
        kCount,
    };

    static constexpr std::size_t kStore = sizeof(FixedStr<kWpaEventReasonChars, StrFit::Clip>);
    static constexpr std::size_t kStoreAlign = 2;

    struct ScanResults {
        static constexpr Kind kKind = Kind::ScanResults;
    };

    struct ScanFailed {
        static constexpr Kind kKind = Kind::ScanFailed;
    };

    struct Connected {
        static constexpr Kind kKind = Kind::Connected;
    };

    struct Disconnected {
        static constexpr Kind kKind = Kind::Disconnected;
        std::uint16_t reason = 0;
    };

    struct SsidTempDisabled {
        static constexpr Kind kKind = Kind::SsidTempDisabled;
        FixedStr<kWpaEventReasonChars, StrFit::Clip> reason{};
    };

    struct NetworkNotFound {
        static constexpr Kind kKind = Kind::NetworkNotFound;
    };

    struct AssocReject {
        static constexpr Kind kKind = Kind::AssocReject;
        std::uint16_t status_code = 0;
    };

    struct Terminating {
        static constexpr Kind kKind = Kind::Terminating;
    };

    struct RegdomChange {
        static constexpr Kind kKind = Kind::RegdomChange;
    };

    struct Unhandled {
        static constexpr Kind kKind = Kind::Unhandled;
    };

    using Alternatives = std::tuple<
        ScanResults,
        ScanFailed,
        Connected,
        Disconnected,
        SsidTempDisabled,
        NetworkNotFound,
        AssocReject,
        Terminating,
        RegdomChange,
        Unhandled>;

    Kind kind = Kind::Unhandled;
    std::uint8_t pad_[1]{};
    alignas(kStoreAlign) std::array<std::byte, kStore> store{};
};

static_assert(infra::MessageSum<WpaEvent> && infra::alternatives_are_total<WpaEvent>());
static_assert(sizeof(WpaEvent) == 30 && alignof(WpaEvent) == 2);

[[nodiscard]] WpaEvent wpa_event_from_text(std::string_view text) noexcept;

}
