// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <bit>
#include <charconv>
#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <system_error>
#include <type_traits>

#include "infra/affine.h"
#include "infra/decimal_text.h"
#include "infra/endian.h"
#include "infra/error.h"
#include "infra/hex_text.h"
#include "infra/json_out.h"
#include "infra/seat.h"
#include "proto/mouse_delta.h"
#include "proto/types.h"

namespace mister::xthread {
struct ByteOffset;
struct SlotStride;
}

namespace mister::proto {
class Link;
}

namespace mister::svc {

struct FilterBankIndex;
struct FilterFormat;
enum class FilterEncoding : std::uint8_t;

using ::mister::infra::affine::operator+;
using ::mister::infra::affine::operator-;
using ::mister::infra::affine::operator*;
using ::mister::infra::affine::operator+=;
using ::mister::infra::affine::operator-=;

enum class CddaOrder : std::uint8_t { LittleEndian, BigEndian };

enum class DiscRegion : std::uint8_t { Unknown = 0, Japan = 1, Usa = 2, Europe = 3 };

using proto::JoyMask;
using proto::PlayerIndex;

inline constexpr std::size_t kJoyWords = 32;

inline constexpr unsigned kMaxPlayers = 6;

inline constexpr std::array<PlayerIndex, kMaxPlayers> kPlayers = [] {
    std::array<PlayerIndex, kMaxPlayers> players{};
    for (std::uint8_t i = 0; i < kMaxPlayers; ++i)
        players[i] = PlayerIndex{i};
    return players;
}();

struct PlayerCount {
    std::uint8_t v = 0;

    [[nodiscard]] static constexpr PlayerCount of(std::size_t n) noexcept {
        return PlayerCount{static_cast<std::uint8_t>(n > kMaxPlayers ? kMaxPlayers : n)};
    }

    friend constexpr bool operator==(PlayerCount, PlayerCount) = default;
    friend constexpr auto operator<=>(PlayerCount, PlayerCount) = default;
};

struct PlayerMask {
    std::uint8_t v = 0;

    [[nodiscard]] constexpr PlayerMask with(PlayerIndex player) const noexcept {
        PlayerMask out = *this;
        for (std::uint8_t i = 0; i < kMaxPlayers; ++i) {
            if (PlayerIndex{i} == player)
                out.v = static_cast<std::uint8_t>(v | (1u << i));
        }
        return out;
    }

    [[nodiscard]] constexpr bool has(PlayerIndex player) const noexcept {
        for (std::uint8_t i = 0; i < kMaxPlayers; ++i) {
            if (PlayerIndex{i} == player)
                return (v >> i & 1u) != 0;
        }
        return false;
    }

    [[nodiscard]] constexpr PlayerMask with(PlayerMask other) const noexcept {
        return PlayerMask{static_cast<std::uint8_t>(v | other.v)};
    }

    [[nodiscard]] constexpr proto::ParamSet as_param_set() const noexcept {
        return proto::ParamSet{v};
    }

    [[nodiscard]] constexpr PlayerMask limited_to(PlayerCount slots) const noexcept {
        if (slots.v >= kMaxPlayers)
            return *this;
        return PlayerMask{static_cast<std::uint8_t>(v & ((1u << slots.v) - 1u))};
    }

    [[nodiscard]] constexpr bool empty() const noexcept { return v == 0; }

    [[nodiscard]] infra::DecimalText text() const noexcept { return infra::DecimalText{v}; }

    friend constexpr bool operator==(PlayerMask, PlayerMask) = default;
};

static_assert(kMaxPlayers <= 8, "PlayerMask holds one bit per player slot");

struct Tally {
    TASTY_SEAT_EXEMPT(component);
    std::uint32_t v = 0;

    constexpr void add(Tally by = Tally{1}) noexcept { v += by.v; }

    constexpr void set(Tally n) noexcept { v = n.v; }

    [[nodiscard]] constexpr Tally sum(Tally by) const noexcept { return Tally{v + by.v}; }

    [[nodiscard]] infra::DecimalText text() const noexcept { return infra::DecimalText{v}; }

    friend constexpr void json_field(infra::JsonOut& o, infra::JsonName key, Tally t) noexcept {
        o.field(key, t.v);
    }

    friend constexpr bool operator==(Tally, Tally) = default;
};

struct OsErrno {
    std::uint16_t v = 0;
    friend constexpr bool operator==(OsErrno, OsErrno) = default;
};

struct KeyHash {
    std::uint32_t v = 0;

    [[nodiscard]] constexpr bool is_bound() const noexcept { return v != 0; }

    [[nodiscard]] infra::DecimalText text() const noexcept { return infra::DecimalText{v}; }

    friend constexpr void json_field(infra::JsonOut& o, infra::JsonName key, KeyHash h) noexcept {
        o.field(key, h.v);
    }

    friend constexpr bool operator==(KeyHash, KeyHash) = default;
};

struct TrackIndex;

struct CueField {
    std::uint32_t v = 0;
    friend constexpr bool operator==(CueField, CueField) = default;
    friend constexpr auto operator<=>(CueField, CueField) = default;
};

struct TrackNumber {
    std::uint8_t v = 0;

    template <std::integral I>
    [[nodiscard]] static constexpr TrackNumber of(I n) noexcept {
        return TrackNumber{static_cast<std::uint8_t>(n)};
    }

    constexpr void store_le32(std::span<std::uint8_t, 4> dst) const noexcept {
        infra::store_le<std::uint32_t>(dst, v);
    }

    [[nodiscard]] constexpr std::uint8_t bcd_byte() const noexcept {
        return proto::PackedBcd::unmasked_byte(v);
    }

    [[nodiscard]] constexpr TrackIndex index() const noexcept;

    [[nodiscard]] constexpr bool matches(CueField decoded) const noexcept { return v == decoded.v; }

    friend constexpr bool operator==(TrackNumber, TrackNumber) = default;
    friend constexpr auto operator<=>(TrackNumber, TrackNumber) = default;
};

struct TrackCount {
    std::uint8_t v = 0;
    using affine_count = TrackCount;

    [[nodiscard]] constexpr bool contains(TrackIndex index) const noexcept;

    [[nodiscard]] constexpr TrackCount at_least_one() const noexcept {
        return TrackCount{static_cast<std::uint8_t>(v != 0 ? v : 1)};
    }

    [[nodiscard]] constexpr bool empty() const noexcept { return v == 0; }

    [[nodiscard]] constexpr TrackIndex next_index() const noexcept;
    [[nodiscard]] constexpr TrackIndex last_index() const noexcept;

    [[nodiscard]] constexpr TrackNumber clamped_number(CueField decoded) const noexcept {
        const std::uint8_t last = at_least_one().v;
        if (decoded.v < 1)
            return TrackNumber{1};
        return TrackNumber{decoded.v > last ? last : static_cast<std::uint8_t>(decoded.v)};
    }

    [[nodiscard]] constexpr std::uint8_t bcd_byte() const noexcept {
        return proto::PackedBcd::unmasked_byte(v);
    }

    [[nodiscard]] constexpr std::uint32_t bcd_over_binary_word() const noexcept {
        using Bcd = proto::PackedBcd;
        const std::uint32_t bcd = ((std::uint32_t{v} / Bcd::kRadix) << Bcd::kNibbleBits)
            | (v % Bcd::kRadix);
        return (bcd << kBcdShift) | v;
    }

    static constexpr unsigned kBcdShift = 8;

    [[nodiscard]] constexpr Error refusal(Errc code, infra::ErrorSite at) const noexcept {
        return Error{code, at, v};
    }

    template <class F>
    constexpr void for_each_index(F&& f) const;
    friend constexpr bool operator==(TrackCount, TrackCount) = default;
    friend constexpr auto operator<=>(TrackCount, TrackCount) = default;
};

struct TrackIndex {
    std::uint8_t v = 0;
    using affine_count = TrackCount;

    template <std::integral I>
    [[nodiscard]] static constexpr TrackIndex of(I i) noexcept {
        return TrackIndex{static_cast<std::uint8_t>(i)};
    }

    [[nodiscard]] constexpr xthread::ByteOffset row_offset(
        xthread::SlotStride row_bytes
    ) const noexcept;

    [[nodiscard]] constexpr TrackNumber number() const noexcept {
        return TrackNumber{static_cast<std::uint8_t>(v + 1)};
    }

    friend constexpr bool operator==(TrackIndex, TrackIndex) = default;
    friend constexpr auto operator<=>(TrackIndex, TrackIndex) = default;
};

constexpr TrackIndex TrackNumber::index() const noexcept {
    return TrackIndex{static_cast<std::uint8_t>(v - 1)};
}

constexpr TrackIndex TrackCount::next_index() const noexcept {
    return TrackIndex{v};
}

constexpr TrackIndex TrackCount::last_index() const noexcept {
    return TrackIndex{static_cast<std::uint8_t>(v - 1)};
}

constexpr bool TrackCount::contains(TrackIndex index) const noexcept {
    return index.v < v;
}

template <class F>
constexpr void TrackCount::for_each_index(F&& f) const {
    for (std::uint8_t i = 0; i < v; ++i)
        f(TrackIndex{i});
}

static_assert(
    sizeof(TrackCount) == sizeof(std::uint8_t) && sizeof(TrackIndex) == sizeof(std::uint8_t),
    "track count and index are byte fields beside the raw `Toc::last` they replace"
);
static_assert(infra::affine::Count<TrackCount>, "a track count is a count");
static_assert(infra::affine::Position<TrackIndex>, "a track index is a position over TrackCount");
static_assert(TrackIndex{4}.number() == TrackNumber{5}, "index -> number holds the +1");
static_assert(TrackNumber{1}.index() == TrackIndex{0}, "number -> index holds the -1");
static_assert(
    TrackCount{3}.contains(TrackIndex{2}) && !TrackCount{3}.contains(TrackIndex{3}),
    "contains is the index-vs-count guard"
);

struct EvdevCode {
    std::uint16_t v = 0;

    static constexpr std::uint16_t kVirtualBase = 256;

    [[nodiscard]] constexpr bool is_key() const noexcept { return v < kVirtualBase; }

    static constexpr std::uint16_t kKeyEmuBase = 768;

    [[nodiscard]] constexpr proto::HidUsage hid_usage() const noexcept {
        return proto::HidUsage{static_cast<std::uint8_t>(v)};
    }

    [[nodiscard]] constexpr bool is_unset() const noexcept { return v == 0; }

    [[nodiscard]] constexpr EvdevCode key_emu_min() const noexcept {
        return EvdevCode{
            static_cast<std::uint16_t>(kKeyEmuBase + (static_cast<unsigned>(v) << 1u))
        };
    }

    [[nodiscard]] constexpr EvdevCode key_emu_max() const noexcept {
        return EvdevCode{
            static_cast<std::uint16_t>(kKeyEmuBase + (static_cast<unsigned>(v) << 1u) + 1u)
        };
    }

    template <std::size_t Count>
    [[nodiscard]] constexpr bool is_axis_of() const noexcept {
        return v < Count;
    }

    template <std::size_t Count>
    [[nodiscard]] constexpr std::optional<std::size_t> axis_position() const noexcept {
        if (v >= Count)
            return std::nullopt;
        return std::size_t{v};
    }

    [[nodiscard]] constexpr proto::KeyCode wire_key() const noexcept {
        return proto::KeyCode{std::uint32_t{v}};
    }

    [[nodiscard]] static constexpr EvdevCode of(proto::KeyCode key) noexcept {
        return EvdevCode{
            static_cast<std::uint16_t>(key.v & std::numeric_limits<std::uint16_t>::max())
        };
    }

    [[nodiscard]] constexpr EvdevCode or_bits(EvdevCode other) const noexcept {
        return EvdevCode{static_cast<std::uint16_t>(v | other.v)};
    }

    [[nodiscard]] infra::HexText text() const noexcept {
        return infra::HexText{v, infra::HexCase::Upper};
    }

    friend constexpr bool operator==(EvdevCode, EvdevCode) = default;
    friend constexpr auto operator<=>(EvdevCode, EvdevCode) = default;
};

static_assert(
    EvdevCode{255}.is_key() && !EvdevCode{EvdevCode::kVirtualBase}.is_key(),
    "the boundary is 256: codes below it are keys"
);

struct DeviceSlot {
    std::uint8_t v = 0;
    static constexpr unsigned kCount = 32;
    friend constexpr bool operator==(DeviceSlot, DeviceSlot) = default;
};

struct Deadzone {
    std::uint8_t v = 0;
    static constexpr std::uint8_t kMax = 64;

    [[nodiscard]] static constexpr std::optional<Deadzone> of(std::int32_t n) noexcept {
        if (n < 0 || n > kMax)
            return std::nullopt;
        return Deadzone{static_cast<std::uint8_t>(n)};
    }

    [[nodiscard]] static constexpr Deadzone clamped(std::uint32_t n) noexcept {
        return Deadzone{static_cast<std::uint8_t>(n > kMax ? kMax : n)};
    }

    [[nodiscard]] constexpr bool swallows(std::int32_t magnitude) const noexcept {
        return magnitude <= v;
    }

    [[nodiscard]] constexpr bool swallows(float magnitude) const noexcept {
        return magnitude <= static_cast<float>(v);
    }

    [[nodiscard]] constexpr float magnitude() const noexcept { return static_cast<float>(v); }

    friend constexpr bool operator==(Deadzone, Deadzone) = default;

    friend constexpr auto operator<=>(Deadzone, Deadzone) = default;
};

static_assert(
    Deadzone::of(0).has_value()
        && Deadzone::of(Deadzone::kMax).has_value()
        && !Deadzone::of(65).has_value(),
    "a deadzone is 0..64"
);

struct HunkIndex;

struct HunkCount {
    std::uint32_t v = 0;
    using affine_count = HunkCount;

    [[nodiscard]] constexpr bool contains(HunkIndex index) const noexcept;

    [[nodiscard]] static constexpr HunkCount of_slots(std::size_t n) noexcept {
        return HunkCount{static_cast<std::uint32_t>(n)};
    }

    [[nodiscard]] constexpr std::size_t bytes_at(std::size_t per_hunk) const noexcept {
        return per_hunk * static_cast<std::size_t>(v);
    }

    friend constexpr bool operator==(HunkCount, HunkCount) = default;
    friend constexpr auto operator<=>(HunkCount, HunkCount) = default;
};

struct HunkIndex {
    std::uint32_t v = 0;
    using affine_count = HunkCount;

    static constexpr std::uint32_t kNone = 0xFFFFFFFFu;

    [[nodiscard]] static constexpr HunkIndex none() noexcept { return HunkIndex{kNone}; }

    [[nodiscard]] constexpr Error refusal(Errc code, infra::ErrorSite at) const noexcept {
        return Error{code, at, v};
    }

    [[nodiscard]] constexpr bool is_none() const noexcept { return v == kNone; }

    friend constexpr bool operator==(HunkIndex, HunkIndex) = default;
    friend constexpr auto operator<=>(HunkIndex, HunkIndex) = default;
};

constexpr bool HunkCount::contains(HunkIndex index) const noexcept {
    return index.v < v;
}

static_assert(
    sizeof(HunkCount) == sizeof(std::uint32_t) && sizeof(HunkIndex) == sizeof(std::uint32_t),
    "hunk positions are the stock u32 hunk number"
);
static_assert(infra::affine::Count<HunkCount>, "a hunk count is a count");
static_assert(infra::affine::Position<HunkIndex>, "a hunk index is a position over HunkCount");
static_assert(
    HunkIndex::none().is_none() && !HunkIndex{0}.is_none(),
    "none() is the only sentinel"
);

struct Crc32 {
    std::uint32_t v = 0;

    static const Crc32 kUndeclared;

    [[nodiscard]] constexpr bool declared() const noexcept { return v != 0; }

    [[nodiscard]] constexpr std::array<std::uint8_t, 4> to_be_bytes() const noexcept {
        return std::bit_cast<std::array<std::uint8_t, 4>>(
            std::endian::native == std::endian::little ? std::byteswap(v) : v
        );
    }

    friend constexpr bool operator==(Crc32, Crc32) = default;
};

inline constexpr Crc32 Crc32::kUndeclared{0};

[[nodiscard]] Crc32 crc32_extend(Crc32 running, std::span<const std::uint8_t> data) noexcept;

struct Vid {
    std::uint16_t v = 0;

    [[nodiscard]] infra::HexText text() const noexcept { return infra::HexText{v}; }

    [[nodiscard]] static std::optional<Vid> parse_hex(std::string_view text) noexcept {
        constexpr std::size_t kDigits = 4;
        constexpr std::size_t kPrefix = 2;
        constexpr int kRadix = 16;
        if (text.size() > 1 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X'))
            text.remove_prefix(kPrefix);
        if (text.empty() || text.size() > kDigits)
            return std::nullopt;
        std::uint16_t value = 0;
        const auto r = std::from_chars(text.data(), text.data() + text.size(), value, kRadix);
        if (r.ec != std::errc{} || r.ptr != text.data() + text.size())
            return std::nullopt;
        return Vid{value};
    }

    friend constexpr bool operator==(Vid, Vid) = default;
};

struct Pid {
    std::uint16_t v = 0;

    [[nodiscard]] infra::HexText text() const noexcept { return infra::HexText{v}; }

    [[nodiscard]] static std::optional<Pid> parse_hex(std::string_view text) noexcept {
        constexpr std::size_t kDigits = 4;
        constexpr std::size_t kPrefix = 2;
        constexpr int kRadix = 16;
        if (text.size() > 1 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X'))
            text.remove_prefix(kPrefix);
        if (text.empty() || text.size() > kDigits)
            return std::nullopt;
        std::uint16_t value = 0;
        const auto r = std::from_chars(text.data(), text.data() + text.size(), value, kRadix);
        if (r.ec != std::errc{} || r.ptr != text.data() + text.size())
            return std::nullopt;
        return Pid{value};
    }

    friend constexpr bool operator==(Pid, Pid) = default;
};

inline constexpr Vid kAnyVid{0};
inline constexpr Pid kAnyPid{0};

struct Generation {
    std::uint32_t v = 0;

    [[nodiscard]] constexpr Generation next() const noexcept { return Generation{v + 1}; }

    friend constexpr bool operator==(Generation, Generation) = default;
    friend constexpr auto operator<=>(Generation, Generation) = default;
};

struct FltFlags {
    std::uint16_t v = 0;
    static constexpr std::uint16_t kEncodingMask = 0x0003;
    static constexpr std::uint16_t kFullPrecisionBit = 0x0004;
    static constexpr std::uint16_t kCoreScanlinesMask = 0x0030;

    [[nodiscard]] constexpr bool present() const noexcept { return v != 0; }

    [[nodiscard]] constexpr FilterEncoding encoding() const noexcept {
        return static_cast<FilterEncoding>(v & kEncodingMask);
    }

    [[nodiscard]] constexpr bool full_precision() const noexcept {
        return (v & kFullPrecisionBit) != 0;
    }

    [[nodiscard]] constexpr bool core_scanlines() const noexcept {
        return (v & kCoreScanlinesMask) != 0;
    }

    [[nodiscard]] constexpr FilterFormat format() const noexcept;
    friend constexpr bool operator==(FltFlags, FltFlags) = default;
};

enum class VolumeStep : std::uint8_t { Quieter, Hold, Louder };

struct CoreVolume {
    std::uint8_t v = 0;
    static constexpr std::uint8_t kAttenMask = proto::Attenuation::kMax;
    static constexpr std::uint8_t kBoostMask = 0x60;
    static constexpr std::uint8_t kBoostStep = 0x20;
    static constexpr std::uint8_t kBoostMax = 0x40;
    static constexpr std::uint8_t kAttenMax = 6;

    [[nodiscard]] constexpr std::uint8_t field_bits() const noexcept {
        return v & (kAttenMask | kBoostMask);
    }

    [[nodiscard]] constexpr CoreVolume attenuation_only() const noexcept {
        return CoreVolume{static_cast<std::uint8_t>(v & kAttenMask)};
    }

    [[nodiscard]] constexpr CoreVolume stepped(
        VolumeStep step,
        bool boost_available
    ) const noexcept {
        std::uint8_t out = v & kAttenMask;
        if (boost_available) {
            unsigned boost = v & kBoostMask;
            if (step == VolumeStep::Quieter) {
                if (boost != 0) {
                    out = 0;
                    boost -= kBoostStep;
                } else if (out < kAttenMax) {
                    ++out;
                }
            }
            if (step == VolumeStep::Louder) {
                if (boost != 0 || out == 0) {
                    boost += kBoostStep;
                    if (boost > kBoostMax)
                        boost = kBoostMax;
                } else if (out > 0) {
                    --out;
                }
            }
            out = static_cast<std::uint8_t>(out | boost);
        } else {
            if (step == VolumeStep::Quieter && out < kAttenMax)
                ++out;
            if (step == VolumeStep::Louder && out > 0)
                --out;
        }
        return CoreVolume{out};
    }

    friend constexpr bool operator==(CoreVolume, CoreVolume) = default;
};

static_assert(sizeof(CoreVolume) == 1, "the core volume is one byte");

struct AudvolByte {
    std::uint8_t v = 0;

    [[nodiscard]] constexpr std::uint16_t word() const noexcept {
        const std::uint16_t w = v;
        return w;
    }

    friend constexpr bool operator==(AudvolByte, AudvolByte) = default;
};

static_assert(sizeof(AudvolByte) == 1);

enum class AfilterEnable : std::uint8_t { Unsupported, Off, On };

inline constexpr std::size_t kFilterWireWords = 15;

struct FilterWordIndex {
    std::uint8_t v = 0;
    friend constexpr bool operator==(FilterWordIndex, FilterWordIndex) = default;
};

struct FilterWordCount {
    std::uint8_t v = 0;

    [[nodiscard]] constexpr bool empty() const noexcept { return v == 0; }

    [[nodiscard]] constexpr std::span<const std::uint16_t> prefix_of(
        std::span<const std::uint16_t> all
    ) const noexcept {
        return all.first(v);
    }

    [[nodiscard]] constexpr FilterWordIndex next_index() const noexcept {
        return FilterWordIndex{v};
    }

    [[nodiscard]] constexpr FilterWordCount with_one_more() const noexcept {
        FilterWordCount out = *this;
        ++out.v;
        return out;
    }

    friend constexpr bool operator==(FilterWordCount, FilterWordCount) = default;
};

struct ShadowMaskWord {
    std::uint32_t v = 0;

    [[nodiscard]] Ex<void> write_to(proto::Link& link) const;
    friend constexpr bool operator==(ShadowMaskWord, ShadowMaskWord) = default;
};

static_assert(sizeof(ShadowMaskWord) == 4);

struct FilterTapCount {
    std::size_t v = 0;
    friend constexpr bool operator==(FilterTapCount, FilterTapCount) = default;
};

struct FilterTapIndex {
    std::size_t v = 0;
    friend constexpr bool operator==(FilterTapIndex, FilterTapIndex) = default;
};

struct FilterEntryWordCount {
    std::size_t v = 0;
    friend constexpr bool operator==(FilterEntryWordCount, FilterEntryWordCount) = default;
};

struct FilterBankWordCount {
    std::size_t v = 0;
    friend constexpr bool operator==(FilterBankWordCount, FilterBankWordCount) = default;
};

struct FilterIteration {
    std::size_t v = 0;
    friend constexpr bool operator==(FilterIteration, FilterIteration) = default;
};

struct FilterAddressGrid {
    unsigned v = 0;
    friend constexpr bool operator==(FilterAddressGrid, FilterAddressGrid) = default;
};

struct FilterBankWordIndex {
    std::size_t v = 0;

    [[nodiscard]] constexpr FilterIteration iteration(FilterTapCount taps) const noexcept {
        return FilterIteration{v / taps.v};
    }

    [[nodiscard]] constexpr FilterTapIndex tap(FilterTapCount taps) const noexcept {
        return FilterTapIndex{v % taps.v};
    }

    [[nodiscard]] constexpr FilterBankWordIndex pair(
        FilterEntryWordCount words_per_entry
    ) const noexcept {
        return FilterBankWordIndex{v / words_per_entry.v};
    }

    [[nodiscard]] constexpr bool is_address(FilterEntryWordCount words_per_entry) const noexcept {
        return v % words_per_entry.v == 0;
    }

    [[nodiscard]] constexpr FilterBankIndex bank(FilterBankWordCount per_bank) const noexcept;

    [[nodiscard]] constexpr FilterBankWordIndex offset(
        FilterBankWordCount per_bank
    ) const noexcept {
        return FilterBankWordIndex{v % per_bank.v};
    }

    [[nodiscard]] constexpr std::size_t wire_offset(FilterAddressGrid grid) const noexcept {
        return v << grid.v;
    }

    friend constexpr bool operator==(FilterBankWordIndex, FilterBankWordIndex) = default;
};

enum class VscaleMode : std::uint8_t {
    Fit,
    Integer,
    HalfStep,
    QuarterStep,
    ResolutionCoreAspect,
    ResolutionDisplayAspect
};

enum class DirectVideo : std::uint8_t { Off, On, Auto };

[[nodiscard]] constexpr std::uint8_t wire_of(DirectVideo m) noexcept {
    return static_cast<std::uint8_t>(m);
}

[[nodiscard]] constexpr DirectVideo direct_video_of(std::uint8_t raw) noexcept {
    switch (raw) {
        case static_cast<std::uint8_t>(DirectVideo::On):
            return DirectVideo::On;
        case static_cast<std::uint8_t>(DirectVideo::Auto):
            return DirectVideo::Auto;
        default:
            return DirectVideo::Off;
    }
}

inline constexpr std::size_t kNumVmodes = 15;

struct PresetIndex {
    std::uint8_t v = 0;

    [[nodiscard]] static constexpr PresetIndex of(std::uint32_t raw) noexcept {
        return PresetIndex{static_cast<std::uint8_t>(raw < kNumVmodes ? raw : 0)};
    }

    template <class T>
    [[nodiscard]] constexpr const T& in(std::span<const T> table) const noexcept {
        return table[v];
    }

    friend constexpr bool operator==(PresetIndex, PresetIndex) = default;
};

struct OpenFileId;

struct OpenFileCount {
    std::uint16_t v = 0;
    using affine_count = OpenFileCount;

    [[nodiscard]] constexpr bool contains(OpenFileId id) const noexcept;

    [[nodiscard]] constexpr bool empty() const noexcept { return v == 0; }

    [[nodiscard]] constexpr OpenFileId next_index() const noexcept;

    [[nodiscard]] constexpr Error refusal(Errc code, infra::ErrorSite at) const noexcept {
        return Error{code, at, v};
    }

    friend constexpr bool operator==(OpenFileCount, OpenFileCount) = default;
    friend constexpr auto operator<=>(OpenFileCount, OpenFileCount) = default;
};

struct OpenFileId {
    std::uint16_t v = 0;
    using affine_count = OpenFileCount;
    friend constexpr bool operator==(OpenFileId, OpenFileId) = default;
    friend constexpr auto operator<=>(OpenFileId, OpenFileId) = default;
};

constexpr OpenFileId OpenFileCount::next_index() const noexcept {
    return OpenFileId{v};
}

constexpr bool OpenFileCount::contains(OpenFileId id) const noexcept {
    return id.v < v;
}

static_assert(infra::affine::Count<OpenFileCount>, "an open-file count is a count");
static_assert(
    infra::affine::Position<OpenFileId>,
    "an open-file id is a position over OpenFileCount"
);
static_assert(
    OpenFileCount{3}.contains(OpenFileId{2}) && !OpenFileCount{3}.contains(OpenFileId{3}),
    "contains is the id-vs-count guard"
);

namespace adv7513 {

enum class HdrMode : std::uint8_t { Off, Hlg, DciP3 };
}

using MouseMotion = proto::MouseDelta;

static_assert(sizeof(MouseMotion) == 3 * sizeof(int), "three int deltas");

template <std::size_t kWords>
class KeyDownSet {
    TASTY_SEAT_EXEMPT(component);

public:
    static_assert(kWords > 0, "a key-down set holds at least one word");

    constexpr void set(EvdevCode code) noexcept {
        if (!fits(code))
            return;
        words_[word_of(code)] |= bit_of(code);
    }

    constexpr void clear(EvdevCode code) noexcept {
        if (!fits(code))
            return;
        words_[word_of(code)] &= ~bit_of(code);
    }

    [[nodiscard]] constexpr bool has(EvdevCode code) const noexcept {
        if (!fits(code))
            return false;
        return (words_[word_of(code)] & bit_of(code)) != 0;
    }

    template <class Each>
    constexpr void drain(Each&& each) {
        for (std::size_t w = 0; w < kWords; ++w) {
            while (words_[w] != 0u) {
                const unsigned bit = static_cast<unsigned>(std::countr_zero(words_[w]));
                words_[w] &= words_[w] - 1u;
                each(EvdevCode{static_cast<std::uint16_t>(w * kBitsPerWord + bit)});
            }
        }
    }

    friend constexpr bool operator==(const KeyDownSet&, const KeyDownSet&) = default;

private:
    static constexpr unsigned kBitsPerWord = 64;
    static constexpr std::size_t kCodes = kWords * kBitsPerWord;

    static constexpr bool fits(EvdevCode code) noexcept { return code.v < kCodes; }

    static constexpr std::size_t word_of(EvdevCode code) noexcept { return code.v / kBitsPerWord; }

    static constexpr std::uint64_t bit_of(EvdevCode code) noexcept {
        return std::uint64_t{1} << (code.v % kBitsPerWord);
    }

    std::array<std::uint64_t, kWords> words_{};
};

static_assert(
    std::is_trivially_copyable_v<KeyDownSet<14>>,
    "the held-key set is a plain value; it is copied across the drain, never ringed"
);
static_assert(sizeof(KeyDownSet<14>) == 14 * sizeof(std::uint64_t), "14 words == 896 bits");
static_assert(sizeof(KeyDownSet<4>) == 4 * sizeof(std::uint64_t), "4 words == 256 codes");

constexpr bool key_down_set_probe() {
    constexpr EvdevCode kFirstFrameCode{3};
    constexpr EvdevCode kLastAxisCode{895};
    constexpr EvdevCode kAbsentCode{4};
    KeyDownSet<14> s;
    s.set(kFirstFrameCode);
    s.set(kLastAxisCode);
    const bool set_ok = s.has(kFirstFrameCode) && s.has(kLastAxisCode) && !s.has(kAbsentCode);
    s.clear(kFirstFrameCode);
    const bool clear_ok = !s.has(kFirstFrameCode) && s.has(kLastAxisCode);

    KeyDownSet<14> d;
    d.set(kLastAxisCode);
    d.set(kFirstFrameCode);
    EvdevCode first{};
    EvdevCode last{};
    bool once = false;
    d.drain([&](EvdevCode c) {
        if (!once)
            first = c;
        once = true;
        last = c;
    });
    const bool drain_ok = first == kFirstFrameCode
        && last == kLastAxisCode
        && d == KeyDownSet<14>{};
    return set_ok && clear_ok && drain_ok;
}

static_assert(key_down_set_probe(), "set/clear/has and the drain order agree");

}
