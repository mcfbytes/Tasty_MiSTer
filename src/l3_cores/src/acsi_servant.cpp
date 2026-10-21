// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/acsi_servant.h"

#include <algorithm>
#include <cstring>
#include <utility>

#include "cores/st_config.h"
#include "svc/vfs.h"

namespace mister::cores {

namespace {

constexpr std::uint16_t kWriteMemory = 0x08;
constexpr std::uint16_t kReadMemory = 0x09;
constexpr std::uint16_t kAckDma = 0x0A;
constexpr std::uint16_t kNakDma = 0x0B;

constexpr std::uint8_t kCmdTestReady = 0x00;
constexpr std::uint8_t kCmdRequestSense = 0x03;
constexpr std::uint8_t kCmdFormat = 0x04;
constexpr std::uint8_t kCmdRead6 = 0x08;
constexpr std::uint8_t kCmdWrite6 = 0x0A;
constexpr std::uint8_t kCmdInquiry = 0x12;
constexpr std::uint8_t kCmdModeSense = 0x1A;
constexpr std::uint8_t kCmdReadCapacity = 0x25;
constexpr std::uint8_t kCmdRead10 = 0x28;
constexpr std::uint8_t kCmdWrite10 = 0x2A;

constexpr std::uint8_t kStatusGood = 0x00;
constexpr std::uint8_t kStatusCheck = 0x02;

constexpr std::uint8_t kAscNone = 0x00;
constexpr std::uint8_t kAscWriteError = 0x0C;
constexpr std::uint8_t kAscReadError = 0x11;
constexpr std::uint8_t kAscInvalidCommand = 0x20;
constexpr std::uint8_t kAscLbaOutOfRange = 0x21;
constexpr std::uint8_t kAscInvalidLun = 0x25;
constexpr std::uint8_t kAscWriteProtected = 0x27;

constexpr std::uint8_t kSenseKeyIllegalRequest = 0x05;
constexpr std::uint8_t kSenseAdditionalLength = 0x0B;
constexpr std::size_t kSenseBytes = 18;
constexpr std::size_t kCapacityBytes = 8;
constexpr std::uint8_t kBlockSizeCode = 2;
constexpr std::uint8_t kInquiryScsi2 = 2;
constexpr std::uint8_t kInquiryNoDevice = 0x7F;
constexpr std::uint8_t kModeSenseExtentBytes = 8;

constexpr std::string_view kVendor = "MISTer  ";
constexpr std::string_view kRevision = "ATH ";

constexpr std::uint8_t u8(std::byte b) noexcept {
    return std::to_integer<std::uint8_t>(b);
}

constexpr std::byte b8(std::uint32_t v) noexcept {
    return static_cast<std::byte>(v & 0xFFu);
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
    std::string_view serial
) noexcept
    : vfs_(vfs),
      formats_(formats) {
    (void)serial_.assign(serial);
}

Ex<void> AcsiServant::bind_target(
    AcsiTarget t,
    svc::IDiskImage* img,
    std::string_view name
) noexcept {
    if (t.v >= kTargets)
        return std::unexpected(Error{Errc::slot_range, ERR_SITE(), t.v});
    Slot& slot = slot_[t.v];
    slot.image = infra::opt_ref_of(img);
    (void)slot.name.assign(img != nullptr ? name : std::string_view{});
    return {};
}

MountState AcsiServant::bind(std::uint8_t unit, std::string_view path) noexcept {
    TASTY_SEAT_BODY(AcsiServant);
    if (unit >= kTargets)
        return MountState::Failed;
    (void)bind_target(AcsiTarget{unit}, nullptr, {});
    owned_[unit].reset();
    if (path.empty())
        return MountState::Done;
    if (!vfs_)
        return MountState::Failed;
    auto img = svc::open_disk_image(*vfs_, path, formats_, svc::OpenMode::ReadWrite);

    if (!img)
        img = svc::open_disk_image(*vfs_, path, formats_, svc::OpenMode::Read);
    if (!img)
        return MountState::Failed;
    owned_[unit] = std::move(*img);
    (void)bind_target(AcsiTarget{unit}, owned_[unit].get(), StConfig::leaf_of(path));
    return MountState::Done;
}

void AcsiServant::release() noexcept {
    TASTY_SEAT_BODY(AcsiServant);
    forget_command_();
    for (std::uint8_t t = 0; t < kTargets; ++t) {
        (void)bind_target(AcsiTarget{t}, nullptr, {});
        owned_[t].reset();
        slot_[t].asc = 0;
    }
}

proto::DiskAct AcsiServant::start(
    std::span<const std::byte, proto::kDiskStateBytes> state,
    std::span<std::byte, proto::kDiskActBytes> bytes
) noexcept {
    TASTY_SEAT_BODY(AcsiServant);
    forget_command_();
    std::copy(state.begin(), state.end(), state_.begin());
    if ((u8(state_[10]) & 0x01u) == 0)
        return proto::DiskAct{.last = true};
    ++commands_;

    const std::uint8_t target = static_cast<std::uint8_t>(u8(state_[10]) >> 5);
    if (target >= kTargets || !slot_[target].image || slot_[target].image->size().v == 0) {
        ++naks_;
        nak_();
    } else {
        target_ = target;
        dispatch_(slot_[target]);
    }
    proto::DiskAct first = next_act_(bytes);
    first.unit = target;
    return first;
}

proto::DiskAct AcsiServant::resume(std::span<std::byte, proto::kDiskActBytes> bytes) noexcept {
    TASTY_SEAT_BODY(AcsiServant);
    if (transfer_ == Transfer::Write && in_len_ != 0) {
        std::memcpy(chunk_.data() + chunk_off_, bytes.data(), in_len_);
        chunk_off_ += in_len_;
        in_len_ = 0;
        if (chunk_off_ == chunk_len_) {
            Slot& slot = slot_[target_];
            const std::span<const std::byte> window = std::span(chunk_).first(chunk_len_);
            auto put = slot.image ? slot.image->write_at(lba_ * kBlockBytes, window)
                                  : Ex<std::size_t>{std::size_t{0}};
            if (!put || *put != chunk_len_) {
                ++io_faults_;
                transfer_ = Transfer::None;
                answer_(slot, kStatusCheck, kAscWriteError);
                return next_act_(bytes);
            }
            lba_ += chunk_blocks_;
            chunk_len_ = 0;
        }
    }
    return next_act_(bytes);
}

void AcsiServant::forget_command_() noexcept {
    transfer_ = Transfer::None;
    lba_ = 0;
    length_ = 0;
    chunk_blocks_ = 0;
    chunk_len_ = 0;
    chunk_off_ = 0;
    reply_len_ = 0;
    reply_owed_ = false;
    in_len_ = 0;
    status_ = 0;
    ack_owed_ = false;
    nak_owed_ = false;
}

void AcsiServant::memory_write_(std::span<const std::byte> src) {
    reply_len_ = src.size();
    reply_owed_ = true;
}

void AcsiServant::ack_(std::uint8_t status) {
    status_ = status;
    ack_owed_ = true;
}

void AcsiServant::nak_() {
    nak_owed_ = true;
}

void AcsiServant::answer_(Slot& slot, std::uint8_t status, std::uint8_t asc) {
    slot.asc = asc;
    ack_(status);
}

proto::DiskAct AcsiServant::next_act_(std::span<std::byte, proto::kDiskActBytes> bytes) {
    if (std::exchange(reply_owed_, false)) {
        std::memcpy(bytes.data(), reply_.data(), reply_len_);
        const auto n = static_cast<std::uint16_t>(std::exchange(reply_len_, 0));
        return proto::DiskAct{.kind = proto::DiskAct::Kind::Out, .opcode = kWriteMemory, .len = n};
    }
    if (transfer_ == Transfer::Read)
        return read_step_(bytes);
    if (transfer_ == Transfer::Write)
        return write_step_(bytes);
    if (std::exchange(ack_owed_, false)) {
        return proto::DiskAct{
            .kind = proto::DiskAct::Kind::Signal,
            .last = true,
            .has_word = true,
            .opcode = kAckDma,
            .word = status_
        };
    }
    if (std::exchange(nak_owed_, false)) {
        return proto::DiskAct{
            .kind = proto::DiskAct::Kind::Signal,
            .last = true,
            .opcode = kNakDma
        };
    }
    return proto::DiskAct{.last = true};
}

void AcsiServant::dispatch_(Slot& slot) {
    const std::uint8_t cmd = u8(state_[0]);
    const std::uint8_t lun = static_cast<std::uint8_t>(u8(state_[1]) >> 5);
    std::uint64_t lba = (static_cast<std::uint32_t>(u8(state_[1]) & 0x1Fu) << 16)
        | (static_cast<std::uint32_t>(u8(state_[2])) << 8)
        | u8(state_[3]);
    std::uint32_t length = u8(state_[4]);
    if (length == 0)
        length = 256;
    const std::uint64_t blocks = slot.image->size().v / kBlockBytes;
    const auto reply_words = [&] {
        return std::span<const std::byte>(reply_).first((length / 2u) * 2u);
    };
    reply_.fill(std::byte{0});

    switch (cmd) {
        case kCmdReadCapacity: {
            if (lun != 0)
                return answer_(slot, kStatusCheck, kAscInvalidLun);

            const std::uint32_t last = static_cast<std::uint32_t>(blocks) - 1u;
            reply_[0] = b8(last >> 24);
            reply_[1] = b8(last >> 16);
            reply_[2] = b8(last >> 8);
            reply_[3] = b8(last);
            reply_[6] = std::byte{kBlockSizeCode};
            memory_write_(std::span<const std::byte>(reply_).first(kCapacityBytes));
            return answer_(slot, kStatusGood, kAscNone);
        }
        case kCmdTestReady:
        case kCmdFormat:
            if (lun != 0)
                return answer_(slot, kStatusCheck, kAscInvalidLun);
            return answer_(slot, kStatusGood, kAscNone);

        case kCmdRequestSense:
            if (lun != 0)
                slot.asc = kAscInvalidLun;
            reply_[7] = std::byte{kSenseAdditionalLength};
            if (slot.asc != kAscNone) {
                reply_[2] = std::byte{kSenseKeyIllegalRequest};
                reply_[12] = std::byte{slot.asc};
            }
            memory_write_(std::span<const std::byte>(reply_).first(kSenseBytes));
            return answer_(slot, kStatusGood, kAscNone);

        case kCmdRead6:
        case kCmdRead10:
        case kCmdWrite6:
        case kCmdWrite10: {
            if (lun != 0)
                return answer_(slot, kStatusCheck, kAscInvalidLun);
            if (cmd == kCmdRead10 || cmd == kCmdWrite10) {
                lba = (static_cast<std::uint32_t>(u8(state_[2])) << 24)
                    | (static_cast<std::uint32_t>(u8(state_[3])) << 16)
                    | (static_cast<std::uint32_t>(u8(state_[4])) << 8)
                    | u8(state_[5]);
                length = (static_cast<std::uint32_t>(u8(state_[7])) << 8) | u8(state_[8]);
            }
            if (lba + length > blocks)
                return answer_(slot, kStatusCheck, kAscLbaOutOfRange);
            if (cmd == kCmdRead6 || cmd == kCmdRead10)
                return read_blocks_(slot, lba, length);
            return write_blocks_(slot, lba, length);
        }

        case kCmdInquiry: {
            reply_[2] = std::byte{kInquiryScsi2};
            reply_[4] = b8(length - 5u);
            put_text(std::span(reply_).subspan(8, kVendor.size()), kVendor, std::byte{' '});
            put_text(std::span(reply_).subspan(16, 16), {}, std::byte{' '});
            put_text(
                std::span(reply_).subspan(16, kInquiryNameBytes),
                slot.name.view(),
                std::byte{0}
            );
            put_text(std::span(reply_).subspan(32, kRevision.size()), kRevision, std::byte{' '});
            put_text(
                std::span(reply_).subspan(36, kInquirySerialBytes),
                serial_.view(),
                std::byte{' '}
            );
            if (lun != 0)
                reply_[0] = std::byte{kInquiryNoDevice};
            memory_write_(reply_words());
            return answer_(slot, kStatusGood, kAscNone);
        }

        case kCmdModeSense: {
            if (lun != 0)
                return answer_(slot, kStatusCheck, kAscInvalidLun);
            const auto count = static_cast<std::uint32_t>(blocks);
            reply_[3] = std::byte{kModeSenseExtentBytes};
            reply_[5] = b8(count >> 16);
            reply_[6] = b8(count >> 8);
            reply_[7] = b8(count);
            reply_[10] = std::byte{kBlockSizeCode};
            memory_write_(reply_words());
            return answer_(slot, kStatusGood, kAscNone);
        }

        default:
            return answer_(slot, kStatusCheck, kAscInvalidCommand);
    }
}

void AcsiServant::read_blocks_(Slot&, std::uint64_t lba, std::uint32_t length) {
    transfer_ = Transfer::Read;
    lba_ = lba;
    length_ = length;
}

void AcsiServant::write_blocks_(Slot& slot, std::uint64_t lba, std::uint32_t length) {
    if (!slot.image->writable())
        return answer_(slot, kStatusCheck, kAscWriteProtected);
    transfer_ = Transfer::Write;
    lba_ = lba;
    length_ = length;
}

proto::DiskAct AcsiServant::read_step_(std::span<std::byte, proto::kDiskActBytes> bytes) {
    Slot& slot = slot_[target_];
    if (chunk_off_ == chunk_len_) {
        if (length_ == 0) {
            transfer_ = Transfer::None;
            answer_(slot, kStatusGood, kAscNone);
            return next_act_(bytes);
        }
        const std::uint32_t n = std::min(length_, kChunkBlocks);
        const std::size_t want = static_cast<std::size_t>(n) * kBlockBytes;
        const std::span<std::byte> window = std::span(chunk_).first(want);
        auto got = slot.image ? slot.image->read_at(lba_ * kBlockBytes, window)
                              : Ex<std::size_t>{std::size_t{0}};
        if (!got || *got != want) {
            ++io_faults_;
            transfer_ = Transfer::None;
            answer_(slot, kStatusCheck, kAscReadError);
            return next_act_(bytes);
        }
        chunk_len_ = want;
        chunk_off_ = 0;
        lba_ += n;
        length_ -= n;
    }
    const std::size_t k = std::min(proto::kDiskActBytes, chunk_len_ - chunk_off_);
    std::memcpy(bytes.data(), chunk_.data() + chunk_off_, k);
    chunk_off_ += k;
    return proto::DiskAct{
        .kind = proto::DiskAct::Kind::Out,
        .opcode = kWriteMemory,
        .len = static_cast<std::uint16_t>(k)
    };
}

proto::DiskAct AcsiServant::write_step_(std::span<std::byte, proto::kDiskActBytes> bytes) {
    if (chunk_len_ == 0) {
        if (length_ == 0) {
            transfer_ = Transfer::None;
            answer_(slot_[target_], kStatusGood, kAscNone);
            return next_act_(bytes);
        }
        chunk_blocks_ = std::min(length_, kChunkBlocks);
        chunk_len_ = static_cast<std::size_t>(chunk_blocks_) * kBlockBytes;
        chunk_off_ = 0;
        length_ -= chunk_blocks_;
    }
    in_len_ = std::min(proto::kDiskActBytes, chunk_len_ - chunk_off_);
    return proto::DiskAct{
        .kind = proto::DiskAct::Kind::In,
        .opcode = kReadMemory,
        .len = static_cast<std::uint16_t>(in_len_)
    };
}

}
