// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/x86/x86_floppy_servant.h"

#include <algorithm>
#include <span>
#include <utility>

#include "infra/prelude.h"
#include "infra/span_cut.h"
#include "infra/xthread/wake_flag.h"
#include "proto/uio_opcodes.h"
#include "svc/vfs.h"

namespace mister::cores::x86 {

namespace {

constexpr hal::LinkWord kDataOut = proto::word(proto::UioOp::DmaWrite);
constexpr hal::LinkWord kDataIn = proto::word(proto::UioOp::DmaRead);

static_assert(
    FloppyServant::kSector.window_count(proto::BlockCount{1})
    == infra::ByteCount{proto::kDiskActBytes / proto::DiskAct::kWordBytes * std::size_t{2}}
);

}

FloppyServant::FloppyServant(
    infra::OptRef<const svc::Vfs> vfs,
    std::span<const svc::DiskFormatRow> formats,
    proto::DiskUnits drives,
    proto::ReplyIndex params_at,
    infra::OptRef<xthread::WakeFlag> activity
) noexcept
    : vfs_(vfs),
      activity_(activity),
      formats_(formats),
      drives_(drives),
      params_at_(params_at) {
    if (vfs_)
        card_.emplace(*vfs_);
}

MountState FloppyServant::bind(proto::DiskUnit unit, std::string_view path) noexcept {
    TASTY_SEAT_BODY(FloppyServant);
    const auto slot = image_slot_.find(unit);
    if (!slot || !drives_.contains(unit))
        return MountState::Failed;
    transfer_.reset();
    slot->reset();
    if (path.empty())
        return MountState::Done;
    if (!vfs_ || !card_)
        return MountState::Failed;
    auto img = svc::open_disk_image(*card_, path, formats_);
    if (!img)
        img = svc::open_disk_image(*vfs_, path, formats_, svc::OpenMode::Read);
    if (!img)
        return MountState::Failed;
    *slot = std::move(*img);
    return MountState::Done;
}

void FloppyServant::release() noexcept {
    TASTY_SEAT_BODY(FloppyServant);
    transfer_.reset();
    for (std::unique_ptr<svc::IDiskImage>& img : image_slot_)
        img.reset();
}

infra::OptRef<svc::IDiskImage> FloppyServant::image_(proto::DiskUnit drive) noexcept {
    const auto slot = image_slot_.find(drive);
    if (!slot || !*slot)
        return {};
    return infra::opt_ref_of(slot->get());
}

proto::DiskAct FloppyServant::start(
    const proto::user_io::PollFrame& block,
    std::span<std::byte, proto::kDiskActBytes> bytes
) noexcept {
    TASTY_SEAT_BODY(FloppyServant);
    transfer_.reset();
    const auto r = FdcRequest::decode(block, params_at_);
    if (!r)
        return proto::DiskAct::none();
    commands_.add();
    return r->read ? start_read_(*r, bytes) : start_write_(*r);
}

proto::DiskAct FloppyServant::start_read_(
    const FdcRequest& r,
    std::span<std::byte, proto::kDiskActBytes> bytes
) noexcept {
    const auto sector = kSector.block_of(std::span<std::byte>(buffer_));
    std::ranges::fill(sector, std::byte{});
    if (const auto img = image_(r.drive)) {
        const auto got = img->read_at(kSector.position_of(r.lba), sector);
        if (!got) {
            io_faults_.add();
            std::ranges::fill(sector, std::byte{});
        }
    }
    if (activity_)
        activity_->request();
    transfer_ = Transfer{.request = r, .total = kSector.window_count(proto::BlockCount{1})};
    return data_act_(bytes).with_unit(r.drive);
}

proto::DiskAct FloppyServant::start_write_(const FdcRequest& r) noexcept {
    if (r.count == proto::BlockCount{})
        return proto::DiskAct::none();
    if (r.count > kMaxWriteSectors) {
        refused_counts_.add();
        return proto::DiskAct::none();
    }
    if (activity_)
        activity_->request();
    transfer_ = Transfer{.request = r, .total = kSector.window_count(r.count)};
    return proto::DiskAct::in(kDataIn, proto::kDiskActBytes).at(kDataPort).with_unit(r.drive);
}

proto::DiskAct FloppyServant::data_act_(std::span<std::byte, proto::kDiskActBytes> bytes) noexcept {
    const Transfer& t = *transfer_;
    if (!t.request.read)
        return proto::DiskAct::in(kDataIn, proto::kDiskActBytes).at(kDataPort);

    const auto part = infra::affine::first(
        infra::affine::from(std::span(buffer_), t.at),
        kActPayload
    );
    std::ranges::fill(bytes, std::byte{});
    for (std::size_t i = 0; i < part.size(); ++i)
        bytes[i * proto::DiskAct::kWordBytes] = part[i];
    return proto::DiskAct::out(kDataOut, proto::kDiskActBytes).at(kDataPort);
}

proto::DiskAct FloppyServant::resume(std::span<std::byte, proto::kDiskActBytes> bytes) noexcept {
    TASTY_SEAT_BODY(FloppyServant);
    if (!transfer_)
        return proto::DiskAct::none();
    Transfer& t = *transfer_;
    if (!t.request.read)
        take_written_(bytes);
    t.at += kActPayload;
    if (t.at != infra::affine::end_of<infra::ByteOffset>(t.total))
        return data_act_(bytes);
    if (!t.request.read)
        commit_write_();
    transfer_.reset();
    return proto::DiskAct::none();
}

void FloppyServant::take_written_(std::span<const std::byte, proto::kDiskActBytes> bytes) noexcept {
    const auto part = infra::affine::first(
        infra::affine::from(std::span(buffer_), transfer_->at),
        kActPayload
    );
    for (std::size_t i = 0; i < part.size(); ++i)
        part[i] = bytes[i * proto::DiskAct::kWordBytes];
}

void FloppyServant::commit_write_() noexcept {
    const FdcRequest& r = transfer_->request;
    const auto img = image_(r.drive);
    if (!img || !img->writable())
        return;
    const auto block = std::span<const std::byte>(buffer_).first(kSector.window_bytes(r.count));
    const auto put = img->write_at(kSector.position_of(r.lba), block);
    if (!put || *put != infra::ByteCount{block.size()})
        io_faults_.add();
}

}
