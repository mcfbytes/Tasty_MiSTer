// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/acsi_servant.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <utility>

#include "cores/st_config.h"
#include "infra/endian.h"
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
    constexpr std::uint32_t kByteMask = 0xFF;
    return static_cast<std::byte>(v & kByteMask);
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
    const auto found = slot_.find(t);
    if (!found)
        return std::unexpected(Error{Errc::slot_range, ERR_SITE(), t.v});
    Slot& slot = *found;
    slot.image = infra::opt_ref_of(img);
    (void)slot.name.assign(img != nullptr ? name : std::string_view{});
    return {};
}

MountState AcsiServant::bind(proto::DiskUnit unit, std::string_view path) noexcept {
    TASTY_SEAT_BODY(AcsiServant);
    const AcsiTarget target{unit.v};
    const auto found = slot_.find(target);
    if (!found)
        return MountState::Failed;
    Slot& slot = *found;
    (void)bind_target(target, nullptr, {});
    slot.owned.reset();
    if (path.empty())
        return MountState::Done;
    if (!vfs_)
        return MountState::Failed;
    auto img = svc::open_disk_image(*vfs_, path, formats_, svc::OpenMode::ReadWrite);

    if (!img)
        img = svc::open_disk_image(*vfs_, path, formats_, svc::OpenMode::Read);
    if (!img)
        return MountState::Failed;
    slot.owned = std::move(*img);
    (void)bind_target(target, slot.owned.get(), StConfig::leaf_of(path));
    return MountState::Done;
}

void AcsiServant::release() noexcept {
    TASTY_SEAT_BODY(AcsiServant);
    forget_command_();
    for (Slot& slot : slot_) {
        slot.image.reset();
        (void)slot.name.assign(std::string_view{});
        slot.owned.reset();
        slot.asc = 0;
    }
}

proto::DiskAct AcsiServant::start(
    std::span<const std::byte, proto::kDiskStateBytes> state,
    std::span<std::byte, proto::kDiskActBytes> bytes
) noexcept {
    constexpr std::size_t kStateTargetAt = 10;
    TASTY_SEAT_BODY(AcsiServant);
    forget_command_();
    std::copy(state.begin(), state.end(), state_.begin());
    if ((u8(state_[kStateTargetAt]) & 0x01u) == 0)
        return proto::DiskAct::none();
    ++commands_;

    constexpr unsigned kLunShift = 5;
    const AcsiTarget target{static_cast<std::uint8_t>(u8(state_[kStateTargetAt]) >> kLunShift)};
    const auto found = slot_.find(target);
    if (!found || !found->image || found->image->size().v == 0) {
        ++naks_;
        nak_();
    } else {
        target_ = target;
        dispatch_(*found);
    }
    return next_act_(bytes).with_unit(proto::DiskUnit{target.v});
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
        return proto::DiskAct::out(kWriteMemory, n);
    }
    if (transfer_ == Transfer::Read)
        return read_step_(bytes);
    if (transfer_ == Transfer::Write)
        return write_step_(bytes);
    if (std::exchange(ack_owed_, false)) {
        return proto::DiskAct::signal(kAckDma, true, static_cast<std::uint16_t>(status_));
    }
    if (std::exchange(nak_owed_, false)) {
        return proto::DiskAct::signal(kNakDma, true, std::nullopt);
    }
    return proto::DiskAct::none();
}

void AcsiServant::dispatch_(Slot& slot) {
    constexpr std::uint32_t kCdb6LbaMask = 0x1FFFFF;
    constexpr std::size_t kCdb6LengthAt = 4;
    constexpr std::uint32_t kCdb6ZeroLengthBlocks = 256;
    constexpr std::size_t kCdb10LbaAt = 2;
    constexpr std::size_t kCdb10LengthAt = 7;
    constexpr std::size_t kCapacityBlockSizeAt = 6;
    constexpr std::size_t kSenseKeyAt = 2;
    constexpr std::size_t kSenseLengthAt = 7;
    constexpr std::size_t kSenseAscAt = 12;
    constexpr std::size_t kInquiryVersionAt = 2;
    constexpr std::size_t kInquiryLengthAt = 4;
    constexpr std::uint32_t kInquiryHeaderBytes = 5;
    constexpr std::size_t kInquiryVendorAt = 8;
    constexpr std::size_t kInquiryProductAt = 16;
    constexpr std::size_t kInquiryProductBytes = 16;
    constexpr std::size_t kInquiryRevisionAt = 32;
    constexpr std::size_t kInquirySerialAt = 36;
    constexpr std::size_t kModeExtentLengthAt = 3;
    constexpr std::size_t kModeBlockCountAt = 5;
    constexpr std::size_t kModeBlockCountBytes = 3;
    constexpr std::size_t kModeBlockSizeAt = 10;
    constexpr std::uint32_t kWordBytes = 2;
    const std::uint8_t cmd = u8(state_[0]);
    const std::span<const std::byte, proto::kDiskStateBytes> cdb(state_);
    constexpr unsigned kLunShift = 5;
    constexpr std::size_t kCdbWordBytes = sizeof(std::uint32_t);
    const std::uint8_t lun = static_cast<std::uint8_t>(u8(state_[1]) >> kLunShift);
    std::uint64_t lba = infra::load_be<std::uint32_t>(cdb.first<kCdbWordBytes>()) & kCdb6LbaMask;
    std::uint32_t length = u8(state_[kCdb6LengthAt]);
    if (length == 0)
        length = kCdb6ZeroLengthBlocks;
    const std::uint64_t blocks = slot.image->size().v / kBlockBytes;
    const auto block_count = static_cast<std::uint32_t>(blocks);
    const auto reply_words = [&] {
        return std::span<const std::byte>(reply_).first((length / kWordBytes) * kWordBytes);
    };
    reply_.fill(std::byte{0});

    switch (cmd) {
        case kCmdReadCapacity: {
            if (lun != 0)
                return answer_(slot, kStatusCheck, kAscInvalidLun);

            const std::uint32_t last = block_count - 1u;
            infra::store_be<std::uint32_t>(std::span(reply_).first<kCdbWordBytes>(), last);
            reply_[kCapacityBlockSizeAt] = std::byte{kBlockSizeCode};
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
            reply_[kSenseLengthAt] = std::byte{kSenseAdditionalLength};
            if (slot.asc != kAscNone) {
                reply_[kSenseKeyAt] = std::byte{kSenseKeyIllegalRequest};
                reply_[kSenseAscAt] = std::byte{slot.asc};
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
                lba = infra::load_be<std::uint32_t>(cdb.subspan<kCdb10LbaAt, kCdbWordBytes>());
                length = infra::load_be<std::uint16_t>(
                    cdb.subspan<kCdb10LengthAt, sizeof(std::uint16_t)>()
                );
            }
            if (lba + length > blocks)
                return answer_(slot, kStatusCheck, kAscLbaOutOfRange);
            if (cmd == kCmdRead6 || cmd == kCmdRead10)
                return read_blocks_(slot, lba, length);
            return write_blocks_(slot, lba, length);
        }

        case kCmdInquiry: {
            reply_[kInquiryVersionAt] = std::byte{kInquiryScsi2};
            reply_[kInquiryLengthAt] = b8(length - kInquiryHeaderBytes);
            put_text(
                std::span(reply_).subspan(kInquiryVendorAt, kVendor.size()),
                kVendor,
                std::byte{' '}
            );
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
            if (lun != 0)
                reply_[0] = std::byte{kInquiryNoDevice};
            memory_write_(reply_words());
            return answer_(slot, kStatusGood, kAscNone);
        }

        case kCmdModeSense: {
            if (lun != 0)
                return answer_(slot, kStatusCheck, kAscInvalidLun);
            reply_[kModeExtentLengthAt] = std::byte{kModeSenseExtentBytes};
            std::array<std::byte, kCdbWordBytes> count_be{};
            infra::store_be<std::uint32_t>(std::span(count_be), block_count);
            std::copy_n(
                count_be.begin() + 1,
                kModeBlockCountBytes,
                reply_.begin() + kModeBlockCountAt
            );
            reply_[kModeBlockSizeAt] = std::byte{kBlockSizeCode};
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
    return proto::DiskAct::out(kWriteMemory, static_cast<std::uint16_t>(k));
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
    return proto::DiskAct::in(kReadMemory, static_cast<std::uint16_t>(in_len_));
}

}
