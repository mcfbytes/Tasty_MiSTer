// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/gcr_track_image.h"

#include <algorithm>
#include <climits>
#include <optional>
#include <utility>

namespace mister::svc {

namespace {

using gcr::Group;
using gcr::TrackLength;

inline constexpr std::byte kFill{0xFF};

enum Aligned : std::size_t { kHeaderLookBytes = 11, kDataLookBytes = 330 };

void align(std::span<const std::byte> src, std::span<std::byte> out) noexcept {
    std::ranges::copy(src, out.begin());
    const std::span<std::byte> block = out.first(src.size());
    constexpr std::byte kTopBit{0x80};
    while ((block.front() & kTopBit) != std::byte{}) {
        std::byte carry{};
        for (std::size_t i = block.size(); i-- > 0;) {
            const std::byte top = block[i] & kTopBit;
            block[i] = (block[i] << 1) | carry;
            carry = top >> (CHAR_BIT - 1);
        }
    }
}

[[nodiscard]] Error refused(Errc code, infra::ErrorSite at) noexcept {
    return Error{code, at, 0};
}

[[nodiscard]] Ex<void> read_held(
    IDiskImage& image,
    proto::FileOffset at,
    std::span<std::byte> dst
) {
    while (!dst.empty()) {
        const auto n = image.read_at(at, dst);
        if (!n)
            return std::unexpected(n.error());
        if (n->empty())
            break;
        at += proto::FileSize::of(*n);
        dst = n->past_in(dst);
    }
    return {};
}

}

Ex<std::unique_ptr<GcrTrackImage>> GcrTrackImage::open(
    std::unique_ptr<IDiskImage> inner,
    GcrLayout layout
) {
    TASTY_SEAT_BODY(GcrTrackImage);
    if (inner == nullptr)
        return std::unexpected(refused(Errc::contract, ERR_SITE()));
    std::unique_ptr<ILayout> chosen;
    switch (layout) {
        case GcrLayout::Sectors: {
            std::array<std::byte, 2> id_bytes{};
            if (auto r = read_held(*inner, gcr::DiskId::kPosition, id_bytes); !r) {
                return std::unexpected(r.error());
            }
            chosen = std::make_unique<Sectors>(
                gcr::SectorMap::of_image(inner->size()),
                gcr::DiskId::load(id_bytes)
            );
            break;
        }
        case GcrLayout::Tracks: {
            auto tracks = Tracks::open(*inner);
            if (!tracks)
                return std::unexpected(tracks.error());
            chosen = std::move(*tracks);
            break;
        }
    }
    return std::unique_ptr<GcrTrackImage>(
        new GcrTrackImage(File{std::move(inner)}, std::move(chosen))
    );
}

GcrTrackImage::GcrTrackImage(File file, std::unique_ptr<ILayout> layout) noexcept
    : file_(std::move(file)),
      layout_(std::move(layout)) {}

FileSize GcrTrackImage::size() const noexcept {
    TASTY_SEAT_BODY(GcrTrackImage);
    return file_.image().size();
}

bool GcrTrackImage::writable() const noexcept {
    TASTY_SEAT_BODY(GcrTrackImage);
    return file_.image().writable();
}

Ex<infra::ByteCount> GcrTrackImage::read_at(proto::FileOffset at, std::span<std::byte> dst) {
    TASTY_SEAT_BODY(GcrTrackImage);
    return file_.image().read_at(at, dst);
}

Ex<infra::ByteCount> GcrTrackImage::write_at(proto::FileOffset at, std::span<const std::byte> src) {
    TASTY_SEAT_BODY(GcrTrackImage);
    return file_.image().write_at(at, src);
}

infra::OptRef<ITrackImage> GcrTrackImage::track_image() noexcept {
    TASTY_SEAT_BODY(GcrTrackImage);
    return infra::OptRef<ITrackImage>{*this};
}

proto::TrackShape GcrTrackImage::track_shape() const noexcept {
    TASTY_SEAT_BODY(GcrTrackImage);
    return gcr::kTrackShape;
}

GcrCaps GcrTrackImage::caps() const noexcept {
    TASTY_SEAT_BODY(GcrTrackImage);
    return layout_->caps();
}

Ex<infra::ByteCount> GcrTrackImage::read_track(
    const proto::TrackShape::Ask& ask,
    std::span<std::byte> dst
) {
    TASTY_SEAT_BODY(GcrTrackImage);
    const infra::ByteCount room{gcr::SectorMap::kSector.window_bytes(ask.units)};
    if (room > infra::ByteCount{gcr::kLongestTrackBytes}
        || room < infra::ByteCount{TrackLength::kPrefixBytes}) {
        return std::unexpected(refused(Errc::contract, ERR_SITE()));
    }
    const gcr::TrackWord word = gcr::TrackWord::of(ask.word);
    auto stored = layout_->read(word, room, file_, buf_);
    if (!stored)
        return std::unexpected(stored.error());

    const std::span<std::byte> work{buf_.work};
    TrackLength length = stored->clamped_to(room);
    infra::ByteCount reply = length.stored();
    if (length.empty()) {

        if (word.drive_1571()) {
            reply = gcr::kEmptyReply1571;
        } else {
            length = TrackLength::of(gcr::kEmptyTrack);
            reply = length.stored();
        }
        std::ranges::fill(infra::affine::first(work, reply), std::byte{});
    }
    length.store(TrackLength::prefix_in(work));
    const std::span<std::byte> reply_bytes = infra::affine::first(work, reply);
    if (dst.size() < reply_bytes.size())
        return std::unexpected(refused(Errc::contract, ERR_SITE()));
    std::ranges::copy(reply_bytes, dst.begin());
    return reply;
}

Ex<void> GcrTrackImage::write_track(
    const proto::TrackShape::Ask& ask,
    std::span<const std::byte> src
) {
    TASTY_SEAT_BODY(GcrTrackImage);
    if (src.size() > gcr::kLongestTrackBytes || src.size() < TrackLength::kPrefixBytes) {
        return std::unexpected(refused(Errc::contract, ERR_SITE()));
    }
    const std::span<std::byte> work{buf_.work};
    std::ranges::copy(src, work.begin());
    const TrackLength length = TrackLength::load(TrackLength::prefix_in(work));
    return layout_->write(gcr::TrackWord::of(ask.word), length, file_, buf_);
}

GcrTrackImage::File::File(std::unique_ptr<IDiskImage> inner) noexcept
    : inner_(std::move(inner)),
      end_(proto::kFileStart + inner_->size()) {}

gcr::ImagePos GcrTrackImage::File::end() const noexcept {
    return gcr::ImagePos::end_of(end_ - proto::kFileStart);
}

Ex<void> GcrTrackImage::File::read(proto::FileOffset at, std::span<std::byte> dst) {
    TASTY_SEAT_BODY(File);
    return read_held(*inner_, at, dst);
}

Ex<proto::FileOffset> GcrTrackImage::File::write(
    proto::FileOffset at,
    std::span<const std::byte> src
) {
    TASTY_SEAT_BODY(File);
    while (!src.empty()) {
        const auto n = inner_->write_at(at, src);
        if (!n)
            return std::unexpected(n.error());
        if (n->empty())
            return std::unexpected(refused(Errc::short_write, ERR_SITE()));
        at += proto::FileSize::of(*n);
        src = n->past_in(src);
    }
    end_ = std::max(end_, at);
    return at;
}

Ex<proto::FileOffset> GcrTrackImage::File::append(std::span<const std::byte> src) {
    return write(end_, src);
}

proto::FileOffset GcrTrackImage::File::end_offset() const noexcept {
    return end_;
}

GcrCaps GcrTrackImage::Sectors::caps() const noexcept {
    TASTY_SEAT_BODY(Sectors);
    return GcrCaps::of_sectors(map_.tracks());
}

Ex<TrackLength> GcrTrackImage::Sectors::read(
    gcr::TrackWord word,
    infra::ByteCount,
    File& file,
    Buffers& buf
) {
    TASTY_SEAT_BODY(Sectors);
    const gcr::HalfTrack at = word.half_track();
    if (at.between_tracks())
        return TrackLength{};
    const gcr::FullTrack track = at.full();
    const proto::BlockCount on_track = map_.sectors_on(track);
    if (on_track == proto::BlockCount{})
        return TrackLength{};

    const std::span<std::byte> sectors = std::span<std::byte>{buf.sectors}.first(
        gcr::SectorMap::kSector.window_bytes(on_track)
    );
    if (auto r = file.read(map_.position_of(track), sectors); !r) {
        return std::unexpected(r.error());
    }
    const std::span<std::byte> stream = std::span<std::byte>{buf.work}.subspan(
        TrackLength::kPrefixBytes
    );
    return TrackLength::of(
        gcr::synthesize_track(gcr::LogicalTrack::of(track, map_.tracks()), id_, sectors, stream)
    );
}

Ex<void> GcrTrackImage::Sectors::write(
    gcr::TrackWord word,
    TrackLength length,
    File& file,
    Buffers& buf
) {
    TASTY_SEAT_BODY(Sectors);
    const gcr::HalfTrack at = word.half_track();
    if (at.between_tracks())
        return {};
    const gcr::FullTrack track = at.full();
    const proto::BlockCount on_track = map_.sectors_on(track);
    if (on_track == proto::BlockCount{})
        return {};

    if (!length.doubled_fits_work())
        return {};

    decode_(length, on_track, buf);
    const std::span<const std::byte> sectors = std::span<const std::byte>{buf.sectors}.first(
        gcr::SectorMap::kSector.window_bytes(on_track)
    );
    if (auto r = file.write(map_.position_of(track), sectors); !r) {
        return std::unexpected(r.error());
    }
    return {};
}

void GcrTrackImage::Sectors::decode_(
    TrackLength length,
    proto::BlockCount on_track,
    Buffers& buf
) noexcept {
    const std::size_t first = TrackLength::kPrefixBytes;
    const std::span<std::byte> work{buf.work};

    length.double_in(work);
    const std::span<std::byte> scan = infra::affine::first(work, length.stored());
    std::ranges::fill(buf.sectors, std::byte{});

    bool sync = false;
    bool started = false;
    std::byte prev{};
    std::size_t off = 0;
    std::size_t ptr = first;
    gcr::SectorNumber sector = gcr::SectorNumber::kNone;
    while (ptr < scan.size()) {
        const std::byte here = work[ptr + off];
        if (prev == gcr::kSync && here == gcr::kSync)
            sync = true;
        if (here == gcr::kSync || !sync) {
            prev = here;
            ++ptr;
            continue;
        }
        std::array<std::byte, kHeaderLookBytes> look{};
        align(work.subspan(ptr + off, kHeaderLookBytes), look);
        const std::array<std::byte, Group::kDataBytes>
            opening = Group::load(std::span<const std::byte>{look}.first<Group::kGcrBytes>())
                          .decode();
        const std::byte mark = opening.front();
        if (!started && mark == gcr::kHeaderMark) {

            off = ptr - first;
            ptr = first;
            started = true;
        }
        if (mark == gcr::kHeaderMark) {
            enum HeaderByte : std::size_t { kSectorAt = 2 };

            sector = gcr::SectorNumber::of(opening[kSectorAt]);
            const std::array<std::byte, Group::kDataBytes>
                ids = Group::load(
                          std::span<const std::byte>{look}
                              .subspan<Group::kGcrBytes, Group::kGcrBytes>()
                )
                          .decode();
            id_ = gcr::DiskId::of_header(std::span<const std::byte>{ids}.first<2>());
        } else if (mark == gcr::kDataMark) {
            if (sector.before() < on_track) {
                std::array<std::byte, kDataLookBytes> coded{};
                align(work.subspan(ptr + off, kDataLookBytes), coded);
                std::array<std::byte, gcr::kDataBlockBytes> block{};
                for (std::size_t g = 0; g < gcr::kDataBlockBytes / Group::kDataBytes; ++g) {
                    const auto data = Group::load(
                                          std::span{coded}
                                              .subspan(g * Group::kGcrBytes)
                                              .first<Group::kGcrBytes>()
                    )
                                          .decode();
                    std::ranges::copy(data, block.begin() + g * Group::kDataBytes);
                }
                const std::span<std::byte> home = std::span<std::byte>{buf.sectors}.subspan(
                    gcr::SectorMap::kSector.window_bytes(sector.before()),
                    gcr::kSectorDataBytes
                );
                std::ranges::copy(
                    std::span<const std::byte>{block}.subspan(1, home.size()),
                    home.begin()
                );
            } else {
                sector = gcr::SectorNumber::kNone;
            }
            ptr += gcr::kSectorDataBytes;
        }
        sync = false;
    }
}

Ex<std::unique_ptr<GcrTrackImage::Tracks>> GcrTrackImage::Tracks::open(IDiskImage& image) {
    std::array<std::byte, gcr::kHeaderBytes> head{};
    if (auto r = read_held(image, proto::kFileStart, head); !r)
        return std::unexpected(r.error());
    const std::optional<gcr::TrackTotal> slots = gcr::track_slots(head);
    if (!slots)
        return std::unexpected(refused(Errc::bad_format, ERR_SITE()));
    std::unique_ptr<Tracks> made{new Tracks(*slots)};

    constexpr std::size_t kTablesRoom = gcr::TrackTotal::kSlots1571.tables_bytes()
                                            .file_word<std::size_t>();
    std::array<std::byte, kTablesRoom> tables{};
    if (auto r = read_held(
            image,
            gcr::kTablesAt,
            infra::affine::first(std::span<std::byte>{tables}, slots->tables_bytes())
        );
        !r) {
        return std::unexpected(r.error());
    }
    const std::span<const std::byte> both = std::span<const std::byte>{tables};
    const std::span<const std::byte> positions = infra::affine::first(both, slots->table_bytes());
    const std::span<const std::byte> speeds = infra::affine::first(
        infra::affine::from(both, infra::ByteOffset{} + slots->table_bytes()),
        slots->table_bytes()
    );
    for (gcr::HalfTrack t{}; slots->holds(t); t = t.next()) {
        made->table_[t] = Slot{
            .at = gcr::ImagePos::load(t.entry_in(positions)),
            .speed = gcr::SpeedZone::load(t.entry_in(speeds))
        };
    }
    return made;
}

GcrCaps GcrTrackImage::Tracks::caps() const noexcept {
    TASTY_SEAT_BODY(Tracks);
    return GcrCaps::of_tracks(slots_);
}

proto::FileOffset GcrTrackImage::Tracks::position_entry_(gcr::HalfTrack track) noexcept {
    return track.position_entry_at();
}

proto::FileOffset GcrTrackImage::Tracks::speed_entry_(gcr::HalfTrack track) const noexcept {
    return track.speed_entry_at(slots_);
}

std::optional<gcr::ImagePos> GcrTrackImage::Tracks::next_entry_past_(
    gcr::ImagePos from
) const noexcept {
    std::optional<gcr::ImagePos> next;
    for (gcr::HalfTrack t{}; slots_.holds(t); t = t.next()) {
        const Slot& slot = table_[t];
        const std::array<gcr::ImagePos, 2> entries{slot.at, slot.speed.as_position()};
        for (const gcr::ImagePos at : entries)
            if (at > from && (!next || at < *next))
                next = at;
    }
    return next;
}

Ex<TrackLength> GcrTrackImage::Tracks::read(
    gcr::TrackWord word,
    infra::ByteCount room,
    File& file,
    Buffers& buf
) {
    TASTY_SEAT_BODY(Tracks);
    const gcr::HalfTrack track = word.half_track();
    if (!slots_.holds(track) || table_[track].at.none())
        return TrackLength{};
    const std::span<std::byte> work{buf.work};
    if (auto r = file.read(table_[track].at.offset(), infra::affine::first(work, room)); !r) {
        return std::unexpected(r.error());
    }
    return TrackLength::load(TrackLength::prefix_in(work));
}

Ex<void> GcrTrackImage::Tracks::write(
    gcr::TrackWord word,
    TrackLength length,
    File& file,
    Buffers& buf
) {
    TASTY_SEAT_BODY(Tracks);
    const gcr::HalfTrack track = word.half_track();
    if (!slots_.holds(track))
        return {};
    Slot& slot = table_[track];
    const gcr::ImagePos at = slot.at;

    const bool may_move = word.drive_1571();
    if (at.none() && !may_move)
        return {};

    if (!length.fits_work())
        return {};

    const gcr::ImagePos file_end = file.end();
    gcr::TrackSpace space{};
    if (!at.none())
        space = at.space_to(std::min(next_entry_past_(at).value_or(file_end), file_end));
    const std::span<std::byte> work{buf.work};
    const std::span<const std::byte> stored = infra::affine::first(work, length.stored());

    Ex<void> outcome{};
    const auto noted = [&outcome](const Ex<proto::FileOffset>& r) {
        if (!r && outcome)
            outcome = std::unexpected(r.error());
    };
    proto::FileOffset next = at.offset();
    if (space.holds(length)) {
        const auto wrote = file.write(at.offset(), stored);
        noted(wrote);
        if (wrote)
            next = *wrote;
    } else {
        if (!may_move)
            return {};
        noted(file.append(stored));
        slot.at = file_end;
        std::array<std::byte, gcr::kTableEntryBytes> entry{};
        slot.at.store(entry);
        noted(file.write(track.position_entry_at(), entry));
        const gcr::SpeedZone speed = gcr::written_speed(word, slots_, length);
        if (slot.speed != speed) {
            slot.speed = speed;
            speed.store(entry);
            noted(file.write(track.speed_entry_at(slots_), entry));
        }
        if (!space.empty()) {

            const std::span<std::byte> old = infra::affine::first(work, space.bytes());
            std::ranges::fill(old, kFill);
            noted(file.write(at.offset(), old));
        }
        space = gcr::TrackSpace::padded_for(length);
        next = file.end_offset();
    }

    const infra::ByteCount unused = space.fill_behind(length);
    if (!unused.empty()) {
        const std::span<std::byte> fill = infra::affine::first(work, unused);
        std::ranges::fill(fill, kFill);
        noted(file.write(next, fill));
    }
    return outcome;
}

}
