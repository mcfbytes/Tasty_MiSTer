// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/adf_codec.h"

#include <algorithm>
#include <cerrno>
#include <utility>

#include "infra/endian.h"
#include "infra/errno.h"
#include "infra/prelude.h"

namespace mister::svc {
namespace {

using Bytes = std::span<const std::byte>;

static_assert(
    AdfCodec::kTrackBytes
        == AdfCodec::kSectorBytes * std::uint64_t{AdfCodec::kSectors} + AdfCodec::kGapBytes,
    "minimig_fdd.cpp:44-50: TRACK_SIZE is eleven sectors and the gap"
);
static_assert(
    AdfCodec::kTrackFileBytes == AdfCodec::kSectorFileBytes * std::uint64_t{AdfCodec::kSectors}
);

constexpr std::byte kClock{0xAA};
constexpr std::byte kData{0x55};

constexpr std::byte kFormat{0xFF};
constexpr std::byte kLastHeaderTrack{159};
constexpr std::byte kLastHeaderSector{10};
constexpr std::byte kMostToGap{AdfCodec::kSectors};
constexpr std::byte kNoneToGap{};

struct WordBytes {
    std::byte hi{};
    std::byte lo{};

    [[nodiscard]] static constexpr WordBytes of(hal::LinkWord w) noexcept {
        std::array<std::byte, sizeof(hal::LinkWord)> le{};
        w.store_le(le);
        return WordBytes{.hi = le[1], .lo = le[0]};
    }

    [[nodiscard]] constexpr WordBytes data() const noexcept {
        return WordBytes{.hi = hi & kData, .lo = lo & kData};
    }

    [[nodiscard]] constexpr WordBytes odd_half() const noexcept {
        return WordBytes{.hi = (hi >> 1) & kData, .lo = (lo >> 1) & kData};
    }

    [[nodiscard]] constexpr WordBytes odd_back() const noexcept {
        return WordBytes{.hi = (hi & kData) << 1, .lo = (lo & kData) << 1};
    }

    [[nodiscard]] constexpr WordBytes clocked() const noexcept {
        return WordBytes{.hi = hi | kClock, .lo = lo | kClock};
    }

    friend constexpr WordBytes operator^(WordBytes a, WordBytes b) noexcept {
        return WordBytes{.hi = a.hi ^ b.hi, .lo = a.lo ^ b.lo};
    }

    friend constexpr WordBytes operator|(WordBytes a, WordBytes b) noexcept {
        return WordBytes{.hi = a.hi | b.hi, .lo = a.lo | b.lo};
    }

    friend constexpr bool operator==(WordBytes, WordBytes) = default;
};

static_assert(WordBytes::of(AdfCodec::kFillWord).hi == WordBytes::of(AdfCodec::kFillWord).lo);

constexpr std::size_t kLongWords = 2;
using Long = std::array<WordBytes, kLongWords>;

constexpr std::size_t kSumWords = kLongWords + kLongWords;

constexpr std::size_t kLabelWords = 16;

constexpr std::size_t kHalfWords = AdfCodec::kSectorFileBytes.file_word<std::size_t>()
    / sizeof(hal::LinkWord);
static_assert(kSumWords + kHalfWords + kHalfWords == AdfCodec::kDataWords);

constexpr Long kFillLong{WordBytes::of(AdfCodec::kFillWord), WordBytes::of(AdfCodec::kFillWord)};

class WordWriter {
public:
    explicit WordWriter(std::span<std::byte> out) noexcept : rest_(out) {}

    void put(WordBytes b) noexcept {
        rest_[0] = b.hi;
        rest_[1] = b.lo;
        rest_ = rest_.subspan(sizeof(hal::LinkWord));
    }

    void put(hal::LinkWord w) noexcept { put(WordBytes::of(w)); }

    void put(const Long& l) noexcept {
        for (const WordBytes b : l)
            put(b);
    }

private:
    std::span<std::byte> rest_;
};

void put_checksum(WordWriter& w, const Long& sum) noexcept {
    w.put(kFillLong);
    for (const WordBytes b : sum)
        w.put(b.clocked());
}

[[nodiscard]] Long read_long(std::span<const hal::LinkWord, kSumWords> words) noexcept {
    const auto odd = words.first<kLongWords>();
    const auto even = words.last<kLongWords>();
    Long l{};
    for (std::size_t i = 0; i < kLongWords; ++i)
        l[i] = WordBytes::of(odd[i]).odd_back() | WordBytes::of(even[i]).data();
    return l;
}

void fold(Long& sum, std::span<const hal::LinkWord> words) noexcept {
    for (std::size_t i = 0; i < words.size(); ++i)
        sum[i % kLongWords] = sum[i % kLongWords] ^ WordBytes::of(words[i]);
}

[[nodiscard]] Long data_of(const Long& l) noexcept {
    Long out{};
    for (std::size_t i = 0; i < kLongWords; ++i)
        out[i] = l[i].data();
    return out;
}

}

AdfCodec::AdfCodec(std::unique_ptr<IFile> file, TrackCount tracks, bool writable) noexcept
    : file_(std::move(file)),
      tracks_(tracks),
      writable_(writable) {}

Ex<std::unique_ptr<IDiskImage>> AdfCodec::open(
    std::unique_ptr<IFile> file,
    FileSize size,
    bool writable
) {
    return std::unique_ptr<IDiskImage>(
        new AdfCodec(std::move(file), TrackCount::of_file(size), writable)
    );
}

FileSize AdfCodec::size() const noexcept {
    TASTY_SEAT_BODY(AdfCodec);
    return tracks_.stream_bytes();
}

bool AdfCodec::writable() const noexcept {
    TASTY_SEAT_BODY(AdfCodec);
    return writable_;
}

AdfCodec::EncodedSector AdfCodec::encode(Track t, Sector s, const SectorFile& data) noexcept {
    EncodedSector out{};
    WordWriter w{out};
    w.put(kFillLong);
    w.put(kSyncWord);
    w.put(kSyncWord);
    const Long fields{
        WordBytes{.hi = kFormat, .lo = t.field()},
        WordBytes{.hi = s.field(), .lo = s.to_gap_field()}
    };
    Long odd{};
    Long even{};
    Long header{};
    for (std::size_t i = 0; i < kLongWords; ++i) {
        odd[i] = fields[i].odd_half();
        even[i] = fields[i].data();
        header[i] = odd[i] ^ even[i];
    }
    w.put(odd);
    w.put(even);
    for (std::size_t i = 0; i < kLabelWords; ++i)
        w.put(kFillWord);
    put_checksum(w, header);

    std::array<WordBytes, kHalfWords> words{};
    for (std::size_t i = 0; i < kHalfWords; ++i)
        words[i] = WordBytes{
            .hi = data[i * sizeof(hal::LinkWord)],
            .lo = data[i * sizeof(hal::LinkWord) + 1]
        };
    Long sum{};
    for (std::size_t i = 0; i < kHalfWords; ++i)
        sum[i % kLongWords] = sum[i % kLongWords] ^ words[i].odd_half() ^ words[i].data();
    put_checksum(w, sum);
    for (const WordBytes d : words)
        w.put(d.odd_half().clocked());
    for (const WordBytes d : words)
        w.put(d.data().clocked());
    return out;
}

AdfCodec::Header AdfCodec::read_header(HeaderWords words) noexcept {

    constexpr std::size_t kFieldsAt = 1;
    constexpr std::size_t kLabelAt = kFieldsAt + kSumWords;
    constexpr std::size_t kHeaderSumAt = kLabelAt + kLabelWords;
    static_assert(kHeaderSumAt + kSumWords == kHeaderWords);
    if (words[0] != kSyncWord)
        return Header{.fault = HeaderFault::SecondSync};
    const auto fields_words = words.subspan<kFieldsAt, kSumWords>();
    Long sum{};
    fold(sum, fields_words);
    const Long field = read_long(fields_words);
    const std::byte format = field[0].hi;
    const std::byte track = field[0].lo;
    const std::byte sector = field[1].hi;
    const std::byte to_gap = field[1].lo;
    const Header fields{
        .track = Track{std::to_integer<std::uint8_t>(track)},
        .sector = Sector{std::to_integer<std::uint8_t>(sector)}
    };
    if (format != kFormat)
        return Header{.fault = HeaderFault::Format};
    if (track > kLastHeaderTrack)
        return Header{.fault = HeaderFault::TrackRange};
    if (sector > kLastHeaderSector)
        return Header{.fault = HeaderFault::SectorRange};
    if (to_gap > kMostToGap || to_gap == kNoneToGap)
        return Header{.fault = HeaderFault::GapRange};
    fold(sum, words.subspan<kLabelAt, kLabelWords>());
    const Long stored = read_long(words.subspan<kHeaderSumAt, kSumWords>());
    if (stored != data_of(sum))
        return Header{.fault = HeaderFault::Checksum};
    return fields;
}

bool AdfCodec::read_data(DataWords words, SectorFile& out) noexcept {
    const auto checksum = words.first<kSumWords>();
    Long stored = read_long(checksum);
    stored[0].lo = WordBytes::of(checksum.last<kLongWords>()[0]).data().lo;
    const auto odd = words.subspan<kSumWords, kHalfWords>();
    const auto even = words.subspan<kSumWords + kHalfWords, kHalfWords>();
    Long sum{};
    fold(sum, odd);
    fold(sum, even);
    for (std::size_t i = 0; i < kHalfWords; ++i) {
        const WordBytes d = WordBytes::of(odd[i]).odd_back() | WordBytes::of(even[i]).data();
        out[i * sizeof(hal::LinkWord)] = d.hi;
        out[i * sizeof(hal::LinkWord) + 1] = d.lo;
    }
    return stored == data_of(sum);
}

Ex<const AdfCodec::EncodedSector*> AdfCodec::sector_(Track t, Sector s) {
    if (cached_ && cached_->track == t && cached_->sector == s)
        return &cached_->bytes;
    SectorFile data{};
    auto got = file_->read_at(file_at(t, s), data);
    if (!got)
        return std::unexpected(got.error());
    if (*got != infra::ByteCount{data.size()})
        return std::unexpected(Error{Errc::io, ERR_SITE(), 0});
    cached_ = Cached{.track = t, .sector = s, .bytes = encode(t, s, data)};
    return &cached_->bytes;
}

Ex<infra::ByteCount> AdfCodec::read_at(proto::FileOffset at, std::span<std::byte> dst) {
    TASTY_SEAT_BODY(AdfCodec);
    const FileSize whole = size();
    std::size_t done = 0;
    while (done < dst.size() && whole.holds(at)) {
        const FileSize from_start = at - proto::kFileStart;
        const Track t = Track::holding(from_start);
        const FileSize in_track = from_start.bytes_past_whole(kTrackBytes);
        std::span<std::byte> rest = dst.subspan(done);
        std::size_t n = 0;
        if (const std::optional<Sector> s = Sector::holding(in_track)) {
            auto bytes = sector_(t, *s);
            if (!bytes)
                return std::unexpected(bytes.error());
            const Bytes from = Bytes{**bytes}.subspan(
                in_track.bytes_past_whole(kSectorBytes).file_word<std::size_t>()
            );
            n = std::min(from.size(), rest.size());
            std::copy_n(from.begin(), n, rest.begin());
        } else {
            n = std::min((kTrackBytes - in_track).file_word<std::size_t>(), rest.size());
            std::fill_n(rest.begin(), n, WordBytes::of(kFillWord).lo);
        }
        done += n;
        at += FileSize{n};
    }
    return infra::ByteCount{done};
}

Ex<infra::ByteCount> AdfCodec::write_at(proto::FileOffset at, std::span<const std::byte> src) {
    TASTY_SEAT_BODY(AdfCodec);
    if (!writable_)
        return std::unexpected(infra::Errno{EROFS}.refusal(Errc::io, ERR_SITE()));
    const FileSize from_start = at - proto::kFileStart;
    const FileSize in_track = from_start.bytes_past_whole(kTrackBytes);
    const Track t = Track::holding(from_start);
    const std::optional<Sector> sector = Sector::holding(in_track);
    if (FileSize::of(src) != kSectorBytes
        || !in_track.bytes_past_whole(kSectorBytes).empty()
        || !sector
        || !size().holds(at)) {
        return std::unexpected(FileSize::of(src).refusal(Errc::bad_format, ERR_SITE()));
    }
    const Sector s = *sector;
    std::array<hal::LinkWord, kDataWords> words{};
    const Bytes field = src.subspan(kDataFieldAt.file_word<std::size_t>());
    for (std::size_t i = 0; i < words.size(); ++i)
        words[i] = hal::LinkWord{infra::load_be<std::uint16_t>(
            field.subspan(i * sizeof(std::uint16_t)).first<sizeof(std::uint16_t)>()
        )};
    SectorFile data{};
    if (!read_data(words, data))
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    if (cached_ && cached_->track == t && cached_->sector == s)
        cached_.reset();
    auto put = file_->write_at(file_at(t, s), data);
    if (!put)
        return std::unexpected(put.error());
    if (*put != infra::ByteCount{data.size()})
        return std::unexpected(Error{Errc::io, ERR_SITE(), 0});
    if (auto f = file_->flush(); !f)
        return std::unexpected(f.error());
    return infra::ByteCount{src.size()};
}

}
