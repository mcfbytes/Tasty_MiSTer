// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/minimig/floppy_servant.h"

#include <algorithm>
#include <array>
#include <ranges>

#include "cores/minimig/floppy_poll.h"
#include "infra/endian.h"
#include "infra/prelude.h"
#include "proto/user_io/poll_frame_decoder.h"
#include "svc/vfs.h"

namespace mister::cores::minimig {

namespace {

using Adf = svc::AdfCodec;

constexpr unsigned kDriveShift = 6;
constexpr std::byte kDriveMask{0x03};

constexpr hal::SpiWord kFifoWords{0x3FFF};
constexpr proto::ReplyMask kDmaActive{0x8000};

constexpr auto kRemappedSyncs = std::to_array<hal::SpiWord>(
    {hal::SpiWord{0x0000}, hal::SpiWord{0x8914}, hal::SpiWord{0xA144}}
);

constexpr hal::SpiWord kStatusOpcode{0x1000};

struct DriveBits {
    hal::SpiWord disk;
    hal::SpiWord writable;
};

constexpr std::array<DriveBits, FloppyServant::kDrives> kDriveBits{{
    {.disk = hal::SpiWord{0x0001}, .writable = hal::SpiWord{0x0010}},
    {.disk = hal::SpiWord{0x0002}, .writable = hal::SpiWord{0x0020}},
    {.disk = hal::SpiWord{0x0004}, .writable = hal::SpiWord{0x0040}},
    {.disk = hal::SpiWord{0x0008}, .writable = hal::SpiWord{0x0080}},
}};

constexpr proto::FioWordCount kMostInWords{proto::kDiskActBytes / proto::DiskAct::kWordBytes};
constexpr std::size_t kWord = sizeof(hal::SpiWord);

struct FloppyRequest {
    using Request = FloppyRequest;
    static constexpr const proto::user_io::PollFrameShape& kFrame = kFloppyPoll.head();
    enum class Verb : std::uint8_t { Read, Write };

    Verb verb = Verb::Read;
    proto::DiskUnit drive{};
    Adf::Track track{};
    hal::SpiWord sync{};
    proto::FioWordCount fifo_words{};
    bool dma_active = false;
    proto::DiskStatusTag tag{};

    [[nodiscard]] static std::optional<FloppyRequest> decode(
        const proto::user_io::PollFrame& block
    ) noexcept {
        if (block.kept != kFloppyReplies)
            return std::nullopt;
        const proto::ReplyWord asked = block.reply(kFloppyRequestReply);
        const hal::SpiWord head = block.replies[kFloppyRequestReply];
        FloppyRequest r{};
        if (asked.intersects(kFloppyReadBit)) {
            r.verb = Verb::Read;
            r.tag = proto::DiskStatusTag::of(asked, kFloppyReadTag);
        } else if (asked.intersects(kFloppyWriteBit)) {
            r.verb = Verb::Write;
            r.tag = proto::DiskStatusTag::of(asked, kFloppyWriteTag);
        } else {
            return std::nullopt;
        }
        r.drive = proto::DiskUnit{
            std::to_integer<std::uint8_t>((std::byte{head.high_byte()} >> kDriveShift) & kDriveMask)
        };
        r.track = Adf::Track::of(head);
        r.sync = block.replies[kFloppySyncReply];
        const hal::SpiWord count = block.replies[kFloppyCountReply];
        r.fifo_words = proto::FioWordCount{count.masked(kFifoWords).bits()};
        r.dma_active = proto::ReplyWord::of(count).intersects(kDmaActive);
        return r;
    }

    [[nodiscard]] hal::SpiWord stream_sync() const noexcept {
        return std::ranges::find(kRemappedSyncs, sync) != kRemappedSyncs.end() ? Adf::kSyncWord
                                                                               : sync;
    }
};

static_assert(proto::user_io::PollFrameDecoder<FloppyRequest>);

void to_wire(std::span<const std::byte> stream, std::span<std::byte> wire) noexcept {
    for (std::size_t i = 0; i + kWord <= stream.size(); i += kWord) {
        const std::uint16_t w = infra::load_be<std::uint16_t>(stream.subspan(i).first<kWord>());
        hal::SpiWord{w}.store_le(wire.subspan(i).first<kWord>());
    }
}

}

FloppyServant::FloppyServant(
    infra::OptRef<const svc::Vfs> vfs,
    std::span<const svc::DiskFormatRow> formats,
    infra::OptRef<xthread::WakeFlag> activity
) noexcept
    : vfs_(vfs),
      activity_(activity),
      formats_(formats) {
    if (vfs_)
        card_.emplace(*vfs_);
}

MountState FloppyServant::bind(proto::DiskUnit unit, std::string_view path) noexcept {
    TASTY_SEAT_BODY(FloppyServant);
    const auto found = drives_.find(unit);
    if (!found)
        return MountState::Failed;
    if (capture_.on(unit))
        capture_.end(FloppyWriteCapture::End::Withdrawn);
    if (reading_ && reading_->unit == unit)
        forget_command_();
    Drive& drive = *found;
    drive = Drive{};
    if (path.empty())
        return MountState::Done;
    if (!vfs_ || !card_)
        return MountState::Failed;
    auto img = svc::open_disk_image(*card_, path, formats_);
    if (!img)
        img = svc::open_disk_image(*vfs_, path, formats_, svc::OpenMode::Read);
    if (!img)
        return MountState::Failed;
    drive.image = std::move(*img);
    drive.tracks = Adf::TrackCount::of_stream(drive.image->size());
    return MountState::Done;
}

void FloppyServant::release() noexcept {
    TASTY_SEAT_BODY(FloppyServant);
    capture_.end(FloppyWriteCapture::End::Withdrawn);
    forget_command_();
    for (Drive& drive : drives_)
        drive = Drive{};
}

proto::DiskAct FloppyServant::standing() const noexcept {
    TASTY_SEAT_BODY(FloppyServant);
    hal::SpiWord word = kStatusOpcode;
    for (const auto& [drive, bits] : std::views::zip(drives_, kDriveBits)) {
        if (!drive.image)
            continue;
        word = word.with(bits.disk);
        if (drive.image->writable())
            word = word.with(bits.writable);
    }
    return proto::DiskAct::signal(word, true, std::nullopt);
}

void FloppyServant::forget_command_() noexcept {
    reading_.reset();
    in_owed_ = proto::FioWordCount{};
}

proto::DiskAct FloppyServant::start(
    const proto::user_io::PollFrame& block,
    std::span<std::byte, proto::kDiskActBytes> bytes
) noexcept {
    TASTY_SEAT_BODY(FloppyServant);
    forget_command_();
    const std::optional<FloppyRequest> req = FloppyRequest::decode(block);
    if (!req)
        return proto::DiskAct::none();
    commands_.add();
    if (activity_)
        activity_->request();
    Drive& drive = drives_[req->drive];
    if (req->verb == FloppyRequest::Verb::Read) {

        capture_.end(FloppyWriteCapture::End::Withdrawn);
        if (!drive.image)
            return proto::DiskAct::none();

        drive.track = req->track.within(drive.tracks);
        if (drive.track_prev != drive.track) {
            drive.track_prev = drive.track;
            drive.sector_offset = Adf::Sector{};
        }
        const Read asked{
            .unit = req->drive,
            .track = drive.track,
            .sector = drive.sector_offset,
            .sync = req->stream_sync(),
            .tag = req->tag
        };
        return read_(asked, bytes).with_unit(req->drive);
    }
    if (!drive.image)
        return proto::DiskAct::none();

    drive.track = req->track;
    drive.track_prev.reset();
    if (!capture_.writes(req->drive, req->track)) {
        capture_.end(FloppyWriteCapture::End::Withdrawn);
        capture_.begin(req->drive, req->track);
    }
    if (req->fifo_words == proto::FioWordCount{}) {

        if (!req->dma_active)
            capture_.end(FloppyWriteCapture::End::Stalled);
        return proto::DiskAct::none();
    }
    in_owed_ = std::min(req->fifo_words, kMostInWords);
    return proto::DiskAct::in(
               hal::SpiWord{},
               static_cast<std::uint16_t>(in_owed_.bytes_at(hal::Width::Word))
    )
        .tagged(req->tag)
        .with_unit(req->drive);
}

proto::DiskAct FloppyServant::resume(std::span<std::byte, proto::kDiskActBytes> bytes) noexcept {
    TASTY_SEAT_BODY(FloppyServant);
    if (in_owed_ != proto::FioWordCount{}) {
        std::array<hal::SpiWord, proto::kDiskActBytes / kWord> words{};
        const std::span<const std::byte> in = std::span<const std::byte>(bytes).first(
            in_owed_.bytes_at(hal::Width::Word)
        );
        std::size_t taken = 0;
        for (; taken * kWord < in.size(); ++taken)
            words[taken] = hal::SpiWord::load_le(in.subspan(taken * kWord).first<kWord>());
        in_owed_ = proto::FioWordCount{};
        Drive& drive = drives_[capture_.unit()];
        if (capture_.open() && drive.image)
            capture_.take(std::span<const hal::SpiWord>(words).first(taken), *drive.image);
        return proto::DiskAct::none();
    }
    if (!reading_)
        return proto::DiskAct::none();
    return next_piece_(bytes);
}

proto::DiskAct FloppyServant::read_(
    const Read& asked,
    std::span<std::byte, proto::kDiskActBytes> bytes
) noexcept {
    reading_ = asked;
    return piece_(true, bytes);
}

proto::DiskAct FloppyServant::next_piece_(
    std::span<std::byte, proto::kDiskActBytes> bytes
) noexcept {
    Read& r = *reading_;
    const proto::FileSize whole = r.gap ? Adf::kGapBytes : Adf::kSectorBytes;
    if (r.sent < whole)
        return piece_(false, bytes);
    if (!r.gap && r.sector.last()) {
        r.gap = true;
        r.sent = proto::FileSize{};
        return piece_(false, bytes);
    }
    r.sector = r.sector.next();
    r.gap = false;
    r.sent = proto::FileSize{};
    drives_[r.unit].sector_offset = r.sector;
    return piece_(true, bytes);
}

proto::DiskAct FloppyServant::piece_(
    bool opens,
    std::span<std::byte, proto::kDiskActBytes> bytes
) noexcept {
    Read& r = *reading_;
    Drive& drive = drives_[r.unit];
    if (!drive.image) {
        forget_command_();
        return proto::DiskAct::none();
    }
    const proto::FileSize whole = r.gap ? Adf::kGapBytes : Adf::kSectorBytes;
    const proto::FileSize len = (whole - r.sent).min(proto::FileSize{proto::kDiskActBytes});
    const proto::FileOffset at = (r.gap ? Adf::gap_at(r.track) : Adf::sector_at(r.track, r.sector))
        + r.sent;
    std::array<std::byte, proto::kDiskActBytes> stream{};
    const std::span<std::byte> piece = std::span(stream).first(len.file_word<std::size_t>());
    auto got = drive.image->read_at(at, piece);
    if (!got || !got->covers(piece)) {
        io_faults_.add();
        forget_command_();
        return proto::DiskAct::none();
    }
    to_wire(piece, bytes);

    if (!r.gap && r.sent.empty()) {
        const std::span<std::byte> sync = std::span<std::byte>(bytes).subspan(
            Adf::kSyncAt.file_word<std::size_t>(),
            Adf::kSyncBytes.file_word<std::size_t>()
        );
        r.sync.store_le(sync.first<kWord>());
        r.sync.store_le(sync.subspan<kWord, kWord>());
    }
    r.sent += len;
    const auto act = proto::DiskAct::out(hal::SpiWord{}, static_cast<std::uint16_t>(piece.size()));
    return opens ? act.tagged(r.tag) : act;
}

}
