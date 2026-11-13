// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/acsi_servant.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <optional>
#include <utility>

#include "cores/st_config.h"
#include "infra/endian.h"
#include "infra/path_parts.h"
#include "svc/vfs.h"

namespace mister::cores {

namespace {

constexpr std::uint8_t kSenseKeyIllegalRequest = 0x05;
constexpr std::uint8_t kSenseAdditionalLength = 0x0B;
constexpr std::size_t kSenseBytes = 18;
constexpr std::size_t kCapacityBytes = 8;
constexpr std::uint8_t kBlockSizeCode = 2;
constexpr std::uint8_t kInquiryScsi2 = 2;
constexpr std::uint8_t kInquiryNoDevice = 0x7F;
constexpr std::uint8_t kModeSenseExtentBytes = 8;

constexpr std::size_t kCapacityBlockSizeAt = 6;
constexpr std::size_t kSenseKeyAt = 2;
constexpr std::size_t kSenseLengthAt = 7;
constexpr std::size_t kSenseAscAt = 12;
constexpr std::size_t kInquiryVersionAt = 2;
constexpr std::size_t kInquiryLengthAt = 4;
constexpr Cdb::AllocBytes kInquiryHeaderBytes{5};
constexpr std::size_t kInquiryVendorAt = 8;
constexpr std::size_t kInquiryProductAt = 16;
constexpr std::size_t kInquiryProductBytes = 16;
constexpr std::size_t kInquiryRevisionAt = 32;
constexpr std::size_t kInquirySerialAt = 36;
constexpr std::size_t kModeExtentLengthAt = 3;
constexpr std::size_t kModeBlockCountAt = 5;
constexpr std::size_t kModeBlockCountBytes = 3;
constexpr std::size_t kModeBlockSizeAt = 10;
constexpr std::size_t kDiskWordBytes = sizeof(std::uint32_t);
constexpr std::size_t kTargetSelectAt = 10;
constexpr unsigned kTargetShift = 5;
constexpr std::uint8_t kUnitSelectBit = 0x01;

constexpr std::string_view kVendor = "MISTer  ";
constexpr std::string_view kRevision = "ATH ";

constexpr std::uint8_t u8(std::byte b) noexcept {
    return std::to_integer<std::uint8_t>(b);
}

struct AcsiState {
    AcsiTarget target{};
    std::optional<Cdb> cdb{};

    [[nodiscard]] static std::optional<AcsiState> decode(
        std::span<const std::byte, proto::kDiskStateBytes> block
    ) noexcept {
        const std::byte select = block[kTargetSelectAt];
        if ((u8(select) & kUnitSelectBit) == 0)
            return std::nullopt;
        return AcsiState{AcsiTarget{u8(select >> kTargetShift)}, Cdb::decode(block)};
    }
};

constexpr std::uint16_t wire_opcode(DmaOp op) noexcept {
    return static_cast<std::uint16_t>(op);
}

constexpr std::uint16_t signal_word(ScsiStatus status) noexcept {
    return static_cast<std::uint16_t>(status);
}

constexpr std::uint16_t wire_len(std::size_t n) noexcept {
    return static_cast<std::uint16_t>(n);
}

constexpr std::uint8_t sense_byte(SenseCode code) noexcept {
    return static_cast<std::uint8_t>(code);
}

void put_text(std::span<std::byte> dst, std::string_view text, std::byte pad) noexcept {
    std::size_t i = 0;
    for (; i < dst.size() && i < text.size(); ++i)
        dst[i] = static_cast<std::byte>(text[i]);
    for (; i < dst.size(); ++i)
        dst[i] = pad;
}

}

AcsiServant::AcsiServant(
    infra::OptRef<const svc::Vfs> vfs,
    std::span<const svc::DiskFormatRow> formats,
    std::string_view serial,
    infra::OptRef<xthread::WakeFlag> activity
) noexcept
    : vfs_(vfs),
      activity_(activity),
      formats_(formats) {
    (void)serial_.assign(serial);
}

Ex<void> AcsiServant::bind_target(
    AcsiTarget t,
    infra::OptRef<svc::IDiskImage> img,
    std::string_view name
) noexcept {
    const auto found = slot_.find(t);
    if (!found)
        return std::unexpected(Error{Errc::slot_range, ERR_SITE(), t.v});
    Slot& slot = *found;
    slot.image.borrow(img);
    (void)slot.name.assign(img ? name : std::string_view{});
    return {};
}

MountState AcsiServant::bind(proto::DiskUnit unit, std::string_view path) noexcept {
    TASTY_SEAT_BODY(AcsiServant);
    const AcsiTarget target = AcsiTarget::of(unit);
    const auto found = slot_.find(target);
    if (!found)
        return MountState::Failed;
    Slot& slot = *found;

    forget_command_();
    (void)bind_target(target, {}, {});
    if (path.empty())
        return MountState::Done;
    if (!vfs_)
        return MountState::Failed;
    auto img = svc::open_disk_image(*vfs_, path, formats_, svc::OpenMode::ReadWrite);

    if (!img)
        img = svc::open_disk_image(*vfs_, path, formats_, svc::OpenMode::Read);
    if (!img)
        return MountState::Failed;
    slot.image.hold(std::move(*img));
    (void)slot.name.assign(infra::basename(path));
    return MountState::Done;
}

void AcsiServant::release() noexcept {
    TASTY_SEAT_BODY(AcsiServant);
    forget_command_();
    for (Slot& slot : slot_) {
        slot.image.clear();
        (void)slot.name.assign(std::string_view{});
        slot.asc = SenseCode::None;
    }
}

proto::DiskAct AcsiServant::start(
    std::span<const std::byte, proto::kDiskStateBytes> state,
    std::span<std::byte, proto::kDiskActBytes> bytes
) noexcept {
    TASTY_SEAT_BODY(AcsiServant);
    forget_command_();
    const auto command = AcsiState::decode(state);
    if (!command)
        return proto::DiskAct::none();
    ++commands_;

    const auto found = slot_.find(command->target);
    infra::OptRef<svc::IDiskImage> image{};
    if (found)
        image = found->image.get();
    if (!image || image->size() == proto::FileSize{}) {
        ++naks_;
        nak_();
    } else {
        target_ = command->target;
        dispatch_(*found, command->cdb);
    }
    return next_act_(bytes).with_unit(command->target.unit());
}

proto::DiskAct AcsiServant::resume(std::span<std::byte, proto::kDiskActBytes> bytes) noexcept {
    TASTY_SEAT_BODY(AcsiServant);
    if (auto* write = std::get_if<WriteTransfer>(&cmd_.transfer)) {

        if (!write_take_in_(*write, bytes))
            cmd_.transfer = std::monostate{};
    }
    return next_act_(bytes);
}

void AcsiServant::forget_command_() noexcept {
    cmd_ = Command{};
}

void AcsiServant::memory_write_(std::span<const std::byte> src) {
    cmd_.reply_len = src.size();
    cmd_.reply_owed = true;
}

void AcsiServant::ack_(ScsiStatus status) {
    cmd_.status = status;
    cmd_.ack_owed = true;
}

void AcsiServant::nak_() {
    cmd_.nak_owed = true;
}

void AcsiServant::answer_(Slot& slot, ScsiStatus status, SenseCode asc) {
    slot.asc = asc;
    ack_(status);
}

svc::IDiskImage& AcsiServant::image_of_(const Slot& slot) noexcept {
    return slot.image.get().value();
}

proto::BlockCount AcsiServant::block_count_(const Slot& slot) const noexcept {
    return proto::end_lba(image_of_(slot).size(), kBlock) - proto::Lba{};
}

proto::DiskAct AcsiServant::next_act_(std::span<std::byte, proto::kDiskActBytes> bytes) {
    if (std::exchange(cmd_.reply_owed, false)) {
        std::memcpy(bytes.data(), reply_.data(), cmd_.reply_len);
        return proto::DiskAct::out(
            wire_opcode(DmaOp::WriteMemory),
            wire_len(std::exchange(cmd_.reply_len, 0))
        );
    }

    if (auto* read = std::get_if<ReadTransfer>(&cmd_.transfer)) {
        const auto act = read_step_(*read, bytes);
        if (act)
            return *act;
        cmd_.transfer = std::monostate{};
        return next_act_(bytes);
    }
    if (auto* write = std::get_if<WriteTransfer>(&cmd_.transfer)) {
        const auto act = write_step_(*write, bytes);
        if (act)
            return *act;
        cmd_.transfer = std::monostate{};
        return next_act_(bytes);
    }
    if (std::exchange(cmd_.ack_owed, false)) {
        return proto::DiskAct::signal(wire_opcode(DmaOp::Ack), true, signal_word(cmd_.status));
    }
    if (std::exchange(cmd_.nak_owed, false)) {
        return proto::DiskAct::signal(wire_opcode(DmaOp::Nak), true, std::nullopt);
    }
    return proto::DiskAct::none();
}

void AcsiServant::dispatch_(Slot& slot, const std::optional<Cdb>& cdb) {
    if (!cdb)
        return answer_(slot, ScsiStatus::Check, SenseCode::InvalidCommand);
    reply_.fill(std::byte{0});
    switch (cdb->opcode) {
        case ScsiOpcode::TestReady:
        case ScsiOpcode::Format:
            return answer_ready_(slot, *cdb);
        case ScsiOpcode::RequestSense:
            return answer_sense_(slot, *cdb);
        case ScsiOpcode::Read6:
        case ScsiOpcode::Read10:
        case ScsiOpcode::Write6:
        case ScsiOpcode::Write10:
            return answer_blocks_(slot, *cdb);
        case ScsiOpcode::Inquiry:
            return answer_inquiry_(slot, *cdb);
        case ScsiOpcode::ModeSense:
            return answer_mode_sense_(slot, *cdb);
        case ScsiOpcode::ReadCapacity:
            return answer_read_capacity_(slot, *cdb);
    }
    return answer_(slot, ScsiStatus::Check, SenseCode::InvalidCommand);
}

void AcsiServant::answer_read_capacity_(Slot& slot, const Cdb& cdb) {
    if (!cdb.lun0())
        return answer_(slot, ScsiStatus::Check, SenseCode::InvalidLun);
    const proto::Lba last = proto::end_lba(image_of_(slot).size(), kBlock).wrapping_prev();
    last.store_be32(std::span(reply_).first<kDiskWordBytes>());
    reply_[kCapacityBlockSizeAt] = std::byte{kBlockSizeCode};
    memory_write_(std::span<const std::byte>(reply_).first(kCapacityBytes));
    return answer_(slot, ScsiStatus::Good, SenseCode::None);
}

void AcsiServant::answer_ready_(Slot& slot, const Cdb& cdb) {
    if (!cdb.lun0())
        return answer_(slot, ScsiStatus::Check, SenseCode::InvalidLun);
    return answer_(slot, ScsiStatus::Good, SenseCode::None);
}

void AcsiServant::answer_sense_(Slot& slot, const Cdb& cdb) {
    if (!cdb.lun0())
        slot.asc = SenseCode::InvalidLun;
    reply_[kSenseLengthAt] = std::byte{kSenseAdditionalLength};
    if (slot.asc != SenseCode::None) {
        reply_[kSenseKeyAt] = std::byte{kSenseKeyIllegalRequest};
        reply_[kSenseAscAt] = std::byte{sense_byte(slot.asc)};
    }
    memory_write_(std::span<const std::byte>(reply_).first(kSenseBytes));
    return answer_(slot, ScsiStatus::Good, SenseCode::None);
}

void AcsiServant::answer_inquiry_(Slot& slot, const Cdb& cdb) {
    reply_[kInquiryVersionAt] = std::byte{kInquiryScsi2};
    reply_[kInquiryLengthAt] = cdb.alloc.reply_length_byte(kInquiryHeaderBytes);
    put_text(std::span(reply_).subspan(kInquiryVendorAt, kVendor.size()), kVendor, std::byte{' '});
    put_text(
        std::span(reply_).subspan(kInquiryProductAt, kInquiryProductBytes),
        {},
        std::byte{' '}
    );
    put_text(
        std::span(reply_).subspan(kInquiryProductAt, kInquiryNameBytes),
        slot.name.view(),
        std::byte{0}
    );
    put_text(
        std::span(reply_).subspan(kInquiryRevisionAt, kRevision.size()),
        kRevision,
        std::byte{' '}
    );
    put_text(
        std::span(reply_).subspan(kInquirySerialAt, kInquirySerialBytes),
        serial_.view(),
        std::byte{' '}
    );
    if (!cdb.lun0())
        reply_[0] = std::byte{kInquiryNoDevice};
    memory_write_(cdb.alloc.words_of(reply_));
    return answer_(slot, ScsiStatus::Good, SenseCode::None);
}

void AcsiServant::answer_mode_sense_(Slot& slot, const Cdb& cdb) {
    if (!cdb.lun0())
        return answer_(slot, ScsiStatus::Check, SenseCode::InvalidLun);
    reply_[kModeExtentLengthAt] = std::byte{kModeSenseExtentBytes};
    std::array<std::byte, kDiskWordBytes> count_be{};
    block_count_(slot).store_be32(std::span(count_be));
    std::copy_n(count_be.begin() + 1, kModeBlockCountBytes, reply_.begin() + kModeBlockCountAt);
    reply_[kModeBlockSizeAt] = std::byte{kBlockSizeCode};
    memory_write_(cdb.alloc.words_of(reply_));
    return answer_(slot, ScsiStatus::Good, SenseCode::None);
}

void AcsiServant::answer_blocks_(Slot& slot, const Cdb& cdb) {
    if (!cdb.lun0())
        return answer_(slot, ScsiStatus::Check, SenseCode::InvalidLun);
    if (!block_count_(slot).holds(cdb.lba, cdb.transfer)) {
        return answer_(slot, ScsiStatus::Check, SenseCode::LbaOutOfRange);
    }
    if (cdb.writes())
        return write_blocks_(slot, cdb);
    return read_blocks_(slot, cdb);
}

void AcsiServant::read_blocks_(Slot&, const Cdb& cdb) {
    if (activity_)
        activity_->request();
    cmd_.transfer = ReadTransfer{.lba = cdb.lba, .length = cdb.transfer};
}

void AcsiServant::write_blocks_(Slot& slot, const Cdb& cdb) {
    if (!image_of_(slot).writable())
        return answer_(slot, ScsiStatus::Check, SenseCode::WriteProtected);
    if (activity_)
        activity_->request();
    cmd_.transfer = WriteTransfer{.lba = cdb.lba, .length = cdb.transfer};
}

std::optional<proto::DiskAct> AcsiServant::read_step_(
    ReadTransfer& arm,
    std::span<std::byte, proto::kDiskActBytes> bytes
) {
    Slot& slot = slot_[target_];

    if (!slot.image.get()) {
        ++io_faults_;
        answer_(slot, ScsiStatus::Check, SenseCode::ReadError);
        return std::nullopt;
    }
    if (cmd_.chunk.off == cmd_.chunk.len) {
        if (arm.length == proto::BlockCount{}) {
            answer_(slot, ScsiStatus::Good, SenseCode::None);
            return std::nullopt;
        }
        const proto::BlockCount n = std::min(arm.length, kChunkBlocks);
        const std::size_t want = kBlock.length_of(n);
        const std::span<std::byte> filled = std::span(chunk_).first(want);

        const auto image = slot.image.get();
        auto got = image ? image->read_at(image_offset(arm.lba), filled)
                         : Ex<std::size_t>{std::size_t{0}};
        if (!got || *got != want) {
            ++io_faults_;
            answer_(slot, ScsiStatus::Check, SenseCode::ReadError);
            return std::nullopt;
        }
        cmd_.chunk.len = want;
        cmd_.chunk.off = 0;
        arm.lba += n;
        arm.length -= n;
    }
    const std::size_t moved = std::min(proto::kDiskActBytes, cmd_.chunk.len - cmd_.chunk.off);
    std::memcpy(bytes.data(), chunk_.data() + cmd_.chunk.off, moved);
    cmd_.chunk.off += moved;
    return proto::DiskAct::out(wire_opcode(DmaOp::WriteMemory), wire_len(moved));
}

std::optional<proto::DiskAct> AcsiServant::write_step_(
    WriteTransfer& arm,
    std::span<std::byte, proto::kDiskActBytes>
) {
    if (cmd_.chunk.len == 0) {
        if (arm.length == proto::BlockCount{}) {
            answer_(slot_[target_], ScsiStatus::Good, SenseCode::None);
            return std::nullopt;
        }
        arm.chunk_blocks = std::min(arm.length, kChunkBlocks);
        cmd_.chunk.len = kBlock.length_of(arm.chunk_blocks);
        cmd_.chunk.off = 0;
        arm.length -= arm.chunk_blocks;
    }
    arm.in_len = std::min(proto::kDiskActBytes, cmd_.chunk.len - cmd_.chunk.off);
    return proto::DiskAct::in(wire_opcode(DmaOp::ReadMemory), wire_len(arm.in_len));
}

bool AcsiServant::write_take_in_(WriteTransfer& arm, std::span<const std::byte> bytes) {
    if (arm.in_len == 0)
        return true;

    Slot& slot = slot_[target_];
    if (!slot.image.get()) {
        ++io_faults_;
        answer_(slot, ScsiStatus::Check, SenseCode::WriteError);
        return false;
    }
    std::memcpy(chunk_.data() + cmd_.chunk.off, bytes.data(), arm.in_len);
    cmd_.chunk.off += arm.in_len;
    arm.in_len = 0;
    if (cmd_.chunk.off != cmd_.chunk.len)
        return true;
    const std::span<const std::byte> filled = std::span(chunk_).first(cmd_.chunk.len);

    const auto image = slot.image.get();
    auto put = image ? image->write_at(image_offset(arm.lba), filled)
                     : Ex<std::size_t>{std::size_t{0}};
    if (!put || *put != cmd_.chunk.len) {
        ++io_faults_;
        answer_(slot, ScsiStatus::Check, SenseCode::WriteError);
        return false;
    }
    arm.lba += arm.chunk_blocks;
    cmd_.chunk.len = 0;
    return true;
}

}
