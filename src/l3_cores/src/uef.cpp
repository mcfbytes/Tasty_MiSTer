// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/uef.h"
#include "cores/uef_tape_file.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "infra/endian.h"
#include "infra/seat.h"

namespace mister::cores::uef {
namespace {

struct TapeImage {
    std::span<const std::uint8_t> v;

    [[nodiscard]] std::size_t size() const noexcept { return v.size(); }

    [[nodiscard]] ChunkId id_at(std::size_t at) const noexcept { return chunk_id(u16_at(at)); }

    [[nodiscard]] std::uint16_t ms_at(std::size_t at) const noexcept { return u16_at(at); }

    [[nodiscard]] std::uint32_t length_at(std::size_t at) const noexcept { return u32_at(at); }

private:
    [[nodiscard]] std::uint16_t u16_at(std::size_t at) const noexcept {
        return infra::load_le<std::uint16_t>(v.subspan(at).first<2>());
    }

    [[nodiscard]] std::uint32_t u32_at(std::size_t at) const noexcept {
        return infra::load_le<std::uint32_t>(v.subspan(at).first<4>());
    }
};

struct Walk {
    std::optional<Located> located;
    TapeBit remaining_bits{};
};

[[nodiscard]] Walk walk_chunks(TapeImage image, ChunkInfo& cache, TapeBit from) noexcept {
    TapeBit bit_pos = from;
    if (bit_pos.within(
            TapeBit{cache.bit_offset_start},
            TapeBitCount{cache.bit_offset_end - cache.bit_offset_start}
        )) {
        return Walk{Located{cache, bit_pos.offset_from(TapeBit{cache.bit_offset_start})}, from};
    }

    std::size_t pos;
    TapeBit chunk_start{};
    if (cache.bit_offset_end != 0 && bit_pos.at_or_past(TapeBit{cache.bit_offset_end})) {
        pos = static_cast<std::size_t>(cache.file_offset) + cache.length;
        chunk_start = TapeBit{cache.bit_offset_end};
        bit_pos.rebase_on(chunk_start);
    } else {
        pos = kHeaderBytes;
    }
    cache.bit_offset_end = 0;

    for (;;) {
        if (pos + kChunkHeaderBytes > image.size())
            break;
        const ChunkId id = image.id_at(pos);

        if (id == ChunkId::ShortRead)
            break;
        const std::uint32_t length = image.length_at(pos + 2);
        pos += kChunkHeaderBytes;
        cache.id = id;
        cache.length = length;
        cache.file_offset = static_cast<std::uint32_t>(pos);

        if (id == ChunkId::Tape
            || id == ChunkId::Gap
            || id == ChunkId::HighTone
            || id == ChunkId::HighDummy) {
            TapeBitCount chunk_bitlen{};
            if (id == ChunkId::Tape) {
                chunk_bitlen = TapeBitCount{length * kBitsPerTapeByte};
            } else if (id == ChunkId::Gap || id == ChunkId::HighTone) {
                if (pos + 2 > image.size())
                    break;
                chunk_bitlen = TapeBitCount{ms_to_bits(image.ms_at(pos))};
            } else {
                if (pos + 4 > image.size())
                    break;
                cache.pre_carrier = ms_to_bits(image.ms_at(pos));
                const std::uint32_t post_carrier = ms_to_bits(image.ms_at(pos + 2));
                chunk_bitlen = TapeBitCount{
                    cache.pre_carrier + kHighDummyCarrierBits + post_carrier
                };
            }

            if (bit_pos.before_count(chunk_bitlen)) {
                cache.record(chunk_start, chunk_bitlen);
                return Walk{Located{cache, bit_pos.chunk_offset()}, bit_pos};
            }
            bit_pos.rewind(chunk_bitlen);
            chunk_start.advance(chunk_bitlen);
        }

        pos = static_cast<std::size_t>(cache.file_offset) + cache.length;
    }

    return Walk{std::nullopt, bit_pos};
}

struct TapeByte {
    std::uint32_t v = 0;

    [[nodiscard]] static TapeByte at(proto::FileSize off) noexcept {
        return TapeByte{off.file_word<std::uint32_t>()};
    }

    [[nodiscard]] std::array<TapeBit, std::numeric_limits<std::uint8_t>::digits>
    bits() const noexcept {
        constexpr std::uint32_t kBits = std::numeric_limits<std::uint8_t>::digits;
        std::array<TapeBit, kBits> out{};
        for (std::uint32_t i = 0; i < kBits; ++i)
            out[i] = TapeBit{(v * kBits) + i};
        return out;
    }
};

struct OutputByte {
    std::uint8_t v = 0;

    constexpr void append(std::uint8_t level) noexcept {
        v = static_cast<std::uint8_t>((v << 1) | level);
    }

    void copy_to(std::byte* dst) const noexcept { std::memcpy(dst, &v, 1); }
};

}

namespace detail {

struct TapeState {
    std::vector<std::uint8_t> image;
    ChunkInfo cache;
    TapeBits bits;
};

namespace {

[[nodiscard]] std::optional<Located> locate(TapeState& t, TapeBit at) {
    const TapeImage image{t.image};
    return walk_chunks(image, t.cache, at).located;
}

[[nodiscard]] bool level_high(TapeState& t, TapeBit at) {
    const std::optional<Located> located = locate(t, at);
    if (!located)
        return false;
    const ChunkInfo& info = located->chunk;
    const ChunkBit pos_in_chunk = located->offset_in_chunk;

    if (info.id == ChunkId::Gap)
        return false;
    if (info.id == ChunkId::HighTone)
        return true;

    if (info.id == ChunkId::Tape) {
        return pos_in_chunk.tape_level_high(
            std::span<const std::uint8_t>(t.image).subspan(info.file_offset)
        );
    }

    return pos_in_chunk.carrier_level_high(ChunkBit{info.pre_carrier});
}

}

UefTapeFile::UefTapeFile(std::vector<std::uint8_t>&& image) : v{std::make_unique<TapeState>()} {
    v->image = std::move(image);
    v->bits = sized(v->image);
}

UefTapeFile::~UefTapeFile() = default;

Ex<std::size_t> UefTapeFile::read_at(std::uint64_t off, std::span<std::byte> dst) {
    TASTY_SEAT_BODY(UefTapeFile);
    const proto::FileSize at{off};
    const std::size_t total = v->bits.bytes().file_word<std::size_t>();
    const std::size_t from = at.file_word<std::size_t>();
    if (from >= total)
        return std::size_t{0};
    const std::size_t n = std::min(dst.size(), total - from);
    for (std::size_t i = 0; i < n; ++i) {
        const TapeByte byte = TapeByte::at(at.advance(proto::FileSize{i}));
        OutputByte out{};
        for (const TapeBit bit : byte.bits())
            out.append(level_high(*v, bit) ? kStopBit : kStartBit);
        out.copy_to(&dst[i]);
    }
    return n;
}

Ex<std::size_t> UefTapeFile::write_at(std::uint64_t, std::span<const std::byte>) {
    TASTY_SEAT_BODY(UefTapeFile);
    return std::unexpected(Error{Errc::io, ERR_SITE(), 0});
}

Ex<proto::FileSize> UefTapeFile::size() const {
    TASTY_SEAT_BODY(UefTapeFile);
    return v->bits.bytes();
}

Ex<void> UefTapeFile::flush() {
    TASTY_SEAT_BODY(UefTapeFile);
    return {};
}

}

bool ChunkBit::tape_level_high(std::span<const std::uint8_t> tape) const noexcept {
    const std::uint32_t slot = frame_slot();
    if (slot == kStartSlot)
        return false;
    if (slot == kStopSlot)
        return true;
    const std::size_t byte_at = data_byte();

    const std::uint8_t byte = byte_at < tape.size() ? tape[byte_at] : std::uint8_t{0};
    const std::uint32_t shift = slot - 1u;
    return (byte & (1u << shift)) != 0;
}

bool ChunkBit::carrier_level_high(ChunkBit carrier) const noexcept {
    if (v < carrier.v || v >= carrier.v + kHighDummyCarrierBits)
        return true;
    const std::uint32_t slot = ChunkBit{v - carrier.v}.frame_slot();
    if (slot == kStartSlot)
        return false;
    if (slot == kStopSlot)
        return true;
    const std::uint32_t shift = slot - 1u;
    return (kHighDummyByte & (1u << shift)) != 0;
}

std::uint32_t ChunkBit::frame_slot() const noexcept {
    return v % kBitsPerTapeByte;
}

std::size_t ChunkBit::data_byte() const noexcept {
    return v / kBitsPerTapeByte;
}

bool valid_header(std::span<const std::uint8_t> image) noexcept {
    if (image.size() < kHeaderBytes)
        return false;
    static constexpr std::array<std::uint8_t, 10>
        kTag{'U', 'E', 'F', ' ', 'F', 'i', 'l', 'e', '!', '\0'};
    return std::equal(kTag.begin(), kTag.end(), image.begin());
}

TapeBits sized(std::span<const std::uint8_t> image) noexcept {
    const TapeImage bytes{image};
    ChunkInfo cache{};
    return TapeBits{
        walk_chunks(bytes, cache, TapeBit{TapeBit::kNone}).remaining_bits.sizing_result()
    };
}

std::unique_ptr<svc::IFile> make_tape_file(std::vector<std::uint8_t>&& image) {

    const proto::FileSize bytes{image.size()};
    if (bytes < kMinImageBytes || bytes > kMaxImageBytes || !valid_header(image)) {
        return std::make_unique<detail::UefTapeFile>(std::vector<std::uint8_t>{});
    }
    return std::make_unique<detail::UefTapeFile>(std::move(image));
}

}
