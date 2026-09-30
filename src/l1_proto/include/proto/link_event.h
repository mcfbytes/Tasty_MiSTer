// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <tuple>

#include "infra/error.h"
#include "infra/message_sum.h"
#include "infra/seat.h"
#include "infra/spsc_ring.h"
#include "proto/save_ask.h"
#include "proto/types.h"

namespace mister::proto {

struct LinkEvent {
    TASTY_SEAT_EXEMPT(component);
    enum class Kind : std::uint8_t {
        ReadyEdge,
        ButtonLevel,
        BlockRequest,
        ConfStr,
        SaveBytes,
        Ps2Control,
        CoreMade,
        IdentityMatched,
        IdentityMismatch,
        OsdMask,
        Ps2ControlEnded,
        StartRefused,
        RebootQuiesced,
        kCount,
    };
    static constexpr std::size_t kStore = 12;

    struct ReadyEdge {
        static constexpr Kind kKind = Kind::ReadyEdge;
        bool ready = false;
    };
    struct ButtonLevel {
        static constexpr Kind kKind = Kind::ButtonLevel;
        bool osd = false;
        bool user = false;
    };
    struct BlockRequest {
        static constexpr Kind kKind = Kind::BlockRequest;
        SlotIndex slot{};
        std::uint8_t pad_[3]{};
        Lba lba{};
        std::uint32_t bytes = 0;
    };
    enum class ConfStrRead : std::uint8_t { Whole, Truncated, Refused };
    struct ConfStr {
        static constexpr Kind kKind = Kind::ConfStr;
        std::uint8_t chunks = 0;
        ConfStrRead read{};
        RxSlabId chunk0{};
        RxSlabId chunk1{};
        BindGeneration bind_gen{};
        std::uint16_t bytes = 0;
        Errc err{};
    };
    struct SaveBytes {
        static constexpr Kind kKind = Kind::SaveBytes;
        SaveKind which{};
        SaveStatus status{};
        RxSlabId rel_path{};
        RxSlabId payload{};
        std::uint8_t pad_[2]{};
        CorrelationTag tag{};
    };
    struct Ps2Control {
        static constexpr Kind kKind = Kind::Ps2Control;
        std::uint16_t keyboard = 0;
        std::uint16_t mouse = 0;
    };
    struct CoreMade {
        static constexpr Kind kKind = Kind::CoreMade;
        bool made = false;
        bool restore_owed = false;
        std::uint16_t bind_gen = 0;
        Errc err{};
    };
    struct IdentityMatched {
        static constexpr Kind kKind = Kind::IdentityMatched;
        bool dual_sdram = false;
        std::uint8_t type_byte = 0;
        BindGeneration bind_gen{};
        bool wide = false;
        std::uint8_t pad_{};
    };
    struct IdentityMismatch {
        static constexpr Kind kKind = Kind::IdentityMismatch;
        BindGeneration bind_gen{};
        Errc err{};
        std::uint32_t raw_word = 0;
        bool at_accept = false;
        std::uint8_t pad_[3]{};
    };
    struct OsdMask {
        static constexpr Kind kKind = Kind::OsdMask;
        ::mister::proto::OsdMask mask{};
        bool front_end = false;
        std::uint8_t pad_{};
    };
    struct Ps2ControlEnded {
        static constexpr Kind kKind = Kind::Ps2ControlEnded;
    };
    struct StartRefused {
        static constexpr Kind kKind = Kind::StartRefused;
        BindGeneration bind_gen{};
        Errc err{};
    };

    struct RebootQuiesced {
        static constexpr Kind kKind = Kind::RebootQuiesced;
        std::uint16_t seq = 0;
        std::uint8_t pad_[2]{};
    };

    using Alternatives = std::tuple<ReadyEdge, ButtonLevel, BlockRequest, ConfStr, SaveBytes,
                                    Ps2Control, CoreMade, IdentityMatched, IdentityMismatch,
                                    OsdMask, Ps2ControlEnded, StartRefused, RebootQuiesced>;
    Kind kind = Kind::ReadyEdge;
    std::uint8_t pad_[3]{};
    alignas(4) std::array<std::byte, kStore> store{};
};

static_assert(infra::MessageSum<LinkEvent> && infra::alternatives_are_total<LinkEvent>());
static_assert(sizeof(LinkEvent) == 16 && alignof(LinkEvent) == 4);
static_assert(infra::ordinal(LinkEvent::Kind::kCount) == 13);

inline constexpr std::size_t kLinkRxCapacity = 64;
using LinkRxRing = xthread::SpscRing<LinkEvent, kLinkRxCapacity>;

}  // namespace mister::proto
