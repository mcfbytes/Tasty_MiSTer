// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <span>
#include <string_view>

#include "hal/types.h"
#include "infra/byte_count.h"
#include "infra/byte_offset.h"
#include "infra/span_cut.h"

namespace mister::cores::ide {

inline constexpr infra::ByteCount kAtapiIdentifyBytes{512};

namespace detail {

inline constexpr infra::ByteOffset kAtapiIdentifyModelAt{27 * sizeof(hal::SpiWord)};
inline constexpr infra::ByteCount kAtapiIdentifyModelBytes{20 * sizeof(hal::SpiWord)};

constexpr void identify_text(std::span<std::byte> field, std::string_view text) noexcept {
    std::ranges::fill(field, std::byte{' '});
    for (std::size_t i = 0; i < text.size() && i < field.size(); ++i)
        field[i ^ 1] = std::bit_cast<std::byte>(text[i]);
}

constexpr void identify_words(std::span<std::byte> field, hal::SpiWord value) noexcept {
    const std::size_t words = field.size() / sizeof(hal::SpiWord);
    for (std::size_t i = 0; i < words; ++i)
        value.store_le(field.subspan(i * sizeof(hal::SpiWord)).first<sizeof(hal::SpiWord)>());
}

inline constexpr std::array<std::byte, kAtapiIdentifyBytes.size()> kAtapiIdentifyBase = [] {
    constexpr std::size_t kWord = sizeof(hal::SpiWord);
    constexpr infra::ByteCount kOneWord{kWord};
    constexpr hal::SpiWord kPacketCdRom{0x8580};
    constexpr infra::ByteOffset kSerialAt{10 * kWord};
    constexpr infra::ByteCount kSerialBytes{10 * kWord};
    constexpr std::string_view kSerial = "AOCD00000";
    constexpr infra::ByteOffset kCapabilitiesAt{49 * kWord};
    constexpr hal::SpiWord kLbaSupported{1u << 9};
    constexpr infra::ByteOffset kValidFieldsAt{53 * kWord};
    constexpr hal::SpiWord kValidFields{0x0007};
    constexpr infra::ByteOffset kCycleTimesAt{65 * kWord};
    constexpr infra::ByteCount kCycleTimesBytes{4 * kWord};
    constexpr hal::SpiWord kCycleNanoseconds{120};
    constexpr infra::ByteOffset kAtaVersionsAt{80 * kWord};
    constexpr hal::SpiWord kAtaVersions{0x007E};
    constexpr infra::ByteOffset kCommandSetsAt{82 * kWord};
    constexpr std::array kCommandSets{
        hal::SpiWord{(1u << 9) | (1u << 4)},
        hal::SpiWord{1u << 14},
        hal::SpiWord{1u << 14},
        hal::SpiWord{(1u << 14) | (1u << 9) | (1u << 4)},
        hal::SpiWord{0},
        hal::SpiWord{1u << 14}
    };
    constexpr infra::ByteOffset kHardwareResetAt{93 * kWord};
    constexpr hal::SpiWord kHardwareReset{0x630B};

    std::array<std::byte, kAtapiIdentifyBytes.size()> block{};
    const std::span<std::byte> bytes{block};
    identify_words(infra::affine::first(bytes, kOneWord), kPacketCdRom);
    identify_text(
        infra::affine::first(infra::affine::from(bytes, kSerialAt), kSerialBytes),
        kSerial
    );
    identify_text(
        infra::affine::first(
            infra::affine::from(bytes, kAtapiIdentifyModelAt),
            kAtapiIdentifyModelBytes
        ),
        std::string_view{}
    );
    identify_words(
        infra::affine::first(infra::affine::from(bytes, kCapabilitiesAt), kOneWord),
        kLbaSupported
    );
    identify_words(
        infra::affine::first(infra::affine::from(bytes, kValidFieldsAt), kOneWord),
        kValidFields
    );
    identify_words(
        infra::affine::first(infra::affine::from(bytes, kCycleTimesAt), kCycleTimesBytes),
        kCycleNanoseconds
    );
    identify_words(
        infra::affine::first(infra::affine::from(bytes, kAtaVersionsAt), kOneWord),
        kAtaVersions
    );
    std::span<std::byte> sets = infra::affine::from(bytes, kCommandSetsAt);
    for (const hal::SpiWord set : kCommandSets) {
        identify_words(infra::affine::first(sets, kOneWord), set);
        sets = sets.subspan(kWord);
    }
    identify_words(
        infra::affine::first(infra::affine::from(bytes, kHardwareResetAt), kOneWord),
        kHardwareReset
    );
    return block;
}();

}

[[nodiscard]] constexpr std::array<std::byte, kAtapiIdentifyBytes.size()> atapi_identify(
    std::string_view image_name
) noexcept {
    std::array<std::byte, kAtapiIdentifyBytes.size()> block = detail::kAtapiIdentifyBase;
    detail::identify_text(
        infra::affine::first(
            infra::affine::from(std::span<std::byte>{block}, detail::kAtapiIdentifyModelAt),
            detail::kAtapiIdentifyModelBytes
        ),
        image_name
    );
    return block;
}

static_assert(
    atapi_identify("")[0] == std::byte{0x80} && atapi_identify("")[1] == std::byte{0x85},
    "word 0 names an ATAPI CD-ROM, low byte first"
);
static_assert(
    atapi_identify("ab")[54] == std::byte{'b'} && atapi_identify("ab")[55] == std::byte{'a'},
    "the model field starts at word 27, first character in the high byte"
);
static_assert(
    atapi_identify("abc")[56] == std::byte{' '} && atapi_identify("abc")[57] == std::byte{'c'},
    "an odd name's last word holds a space in its low byte"
);

}
