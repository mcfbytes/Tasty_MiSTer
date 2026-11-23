// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/ide/ide_channel.h"

#include <array>
#include <span>
#include <utility>

#include "cores/ide/atapi_disc.h"
#include "cores/ide/ide_act_wire.h"
#include "cores/ide/ide_engine.h"
#include "infra/ascii.h"
#include "infra/byte_view.h"
#include "infra/path_parts.h"
#include "infra/prelude.h"
#include "svc/disk_image.h"
#include "svc/file.h"
#include "svc/read_bounded.h"
#include "svc/vfs.h"

namespace mister::cores::ide {

namespace {

constexpr std::array<IdeDriveSelect, 2> kDrives{IdeDriveSelect::Master, IdeDriveSelect::Slave};

constexpr std::string_view kVhdExtension = ".vhd";

[[nodiscard]] constexpr bool disk_name(std::string_view path) noexcept {
    return path.size() > kVhdExtension.size() && infra::iends_with(path, kVhdExtension);
}

}

IdeChannel::IdeChannel(
    const IdeChannelRow& row,
    infra::OptRef<const svc::Vfs> vfs,
    std::span<const svc::DiskFormatRow> formats,
    const IGeometryRule& geometry
) noexcept
    : row_(row),
      vfs_(vfs),
      formats_(formats),
      geometry_(geometry),
      wire_(row) {
    if (vfs_)
        card_.emplace(*vfs_);
}

void IdeChannel::configure(IdeDriveSelect drive, std::string_view path) noexcept {
    TASTY_SEAT_BODY(IdeChannel);

    if (open_)
        fatal(Error{Errc::contract, ERR_SITE(), 0}, "IdeChannel: a bind while a command is open");
    if (!configured_.at(drive).assign(path))
        configured_.at(drive).clear();

    if (hot_swap_allowed_(drive) && tray_.at(drive) != Tray::Fixed) {
        hot_mount_(drive);
        return;
    }

    if (!presented_ || last_ == IdeRequest::Reset)
        armed_.at(drive) = true;
}

std::optional<IdeRequest> IdeChannel::asks(const proto::user_io::PollFrame& block) const noexcept {
    return ide_request_of(block.reply(proto::ReplyIndex{}), row_.request);
}

std::optional<svc::VhdSidecar> IdeChannel::sidecar_of_(std::string_view path) const noexcept {
    if (!vfs_)
        return std::nullopt;
    const std::optional<infra::PathText> sidecar_path = svc::vhd_sidecar_path(path);
    if (!sidecar_path)
        return std::nullopt;
    auto file = vfs_->open(sidecar_path->view(), svc::OpenMode::ReadWhole);
    if (!file)
        return std::nullopt;
    const auto text = svc::read_bounded(**file, kSidecarBytes);
    if (!text)
        return std::nullopt;
    const auto parsed = svc::parse_vhd_sidecar(infra::as_text(std::span{*text}));
    if (!parsed)
        return std::nullopt;
    return *parsed;
}

void IdeChannel::mount_(IdeDriveSelect drive, MountWhen when) noexcept {
    TASTY_SEAT_BODY(IdeChannel);
    const std::string_view path = configured_.at(drive).view();
    const bool hot_unit = hot_swap_allowed_(drive) && tray_.at(drive) != Tray::Fixed;
    if (path.empty()) {
        serve_empty_(drive);
        return;
    }
    if (disk_name(path)) {

        if (hot_unit) {
            refused_mounts_.add();
            serve_empty_(drive);
            return;
        }
        mount_disk_(drive);
        return;
    }
    switch (row_.mounts) {
        case MountRule::DiskOnly:
            refused_mounts_.add();
            engine_.eject(drive, wire_);
            tray_.at(drive) = Tray::Fixed;
            mounted_.at(drive) = configured_.at(drive);
            return;
        case MountRule::DiscElseAbsent:
            mount_disc_(drive, when);
            return;
    }
}

void IdeChannel::serve_empty_(IdeDriveSelect drive) noexcept {
    if (empty_tray_(drive) == Tray::Empty) {

        engine_.insert_disc(drive, IdeEngine::DiscMount{}, wire_);
        tray_.at(drive) = Tray::Empty;
    } else {
        engine_.eject(drive, wire_);
        tray_.at(drive) = Tray::Fixed;
    }
    mounted_.at(drive) = configured_.at(drive);
}

void IdeChannel::mount_disk_(IdeDriveSelect drive) noexcept {
    const std::string_view path = configured_.at(drive).view();
    if (!vfs_) {
        mounted_.at(drive) = configured_.at(drive);
        engine_.eject(drive, wire_);
        tray_.at(drive) = Tray::Fixed;
        return;
    }
    std::unique_ptr<svc::IDiskImage> image;
    if (card_) {
        if (auto opened = svc::open_disk_image(*card_, path, formats_))
            image = std::move(*opened);
    }
    if (!image) {
        if (auto opened = svc::open_disk_image(*vfs_, path, formats_, svc::OpenMode::Read))
            image = std::move(*opened);
    }
    if (!image) {

        io_faults_.add();
        engine_.eject(drive, wire_);
        tray_.at(drive) = Tray::Fixed;
        return;
    }
    const proto::FileSize size = image->size();
    const std::optional<svc::VhdSidecar> sidecar = sidecar_of_(path);
    engine_.insert(
        drive,
        IdeEngine::Mount{
            .image = std::move(image),
            .media = IdeMedia::of(size, geometry_.guess(size), sidecar),
            .name = infra::basename(path)
        },
        wire_
    );
    tray_.at(drive) = Tray::Fixed;
    mounted_.at(drive) = configured_.at(drive);
}

void IdeChannel::mount_disc_(IdeDriveSelect drive, MountWhen when) noexcept {
    const std::string_view path = configured_.at(drive).view();
    if (vfs_) {
        if (auto disc = AtapiDisc::open(*vfs_, path)) {
            engine_.insert_disc(
                drive,
                IdeEngine::DiscMount{std::move(*disc), infra::basename(path)},
                wire_
            );
            tray_.at(drive) = Tray::Loaded;
            mounted_.at(drive) = configured_.at(drive);
            return;
        }
    }
    io_faults_.add();
    if (when == MountWhen::AtOnce || hot_swap_allowed_(drive)) {

        engine_.insert_disc(drive, IdeEngine::DiscMount{}, wire_);
        tray_.at(drive) = Tray::Empty;
        if (when == MountWhen::AtOnce)
            mounted_.at(drive) = configured_.at(drive);
        return;
    }
    engine_.eject(drive, wire_);
    tray_.at(drive) = Tray::Fixed;
}

void IdeChannel::hot_mount_(IdeDriveSelect drive) noexcept {
    wire_.clear();
    wire_.begin(row_.units.at(drive));
    armed_.at(drive) = false;
    mount_(drive, MountWhen::AtOnce);
}

void IdeChannel::apply_armed_() noexcept {
    for (const IdeDriveSelect drive : kDrives) {
        const std::string_view path = configured_.at(drive).view();
        const bool empty = path.empty();
        const bool empty_moved = empty && tray_.at(drive) != empty_tray_(drive);
        if (!armed_.at(drive) && !empty_moved)
            continue;
        armed_.at(drive) = false;
        if (!empty && path == mounted_.at(drive).view())
            continue;
        mount_(drive, MountWhen::Reset);
    }
}

void IdeChannel::latch_hot_swap_(const proto::user_io::PollFrame& block) noexcept {
    const proto::ReplyWord head = block.reply(proto::ReplyIndex{});
    for (const IdeDriveSelect drive : kDrives) {
        const std::optional<HotSwapBit>& bit = row_.hot_swap.at(drive);
        allow_.at(drive) = bit && bit->allows(head);
    }
}

proto::DiskAct IdeChannel::start(
    const proto::user_io::PollFrame& block,
    std::span<std::byte, proto::kDiskActBytes> bytes
) noexcept {
    TASTY_SEAT_BODY(IdeChannel);
    const std::optional<IdeRequest> request = asks(block);
    presented_ = true;
    last_ = request;

    if (last_ != IdeRequest::Reset)
        armed_ = {};

    if (!wire_.empty() && last_ != IdeRequest::Reset) {
        open_ = true;
        const proto::DiskAct act = wire_.next(bytes);
        if (act.kind() == proto::DiskAct::Kind::None)
            open_ = false;
        return act;
    }
    wire_.clear();
    open_ = false;
    if (!request) {
        if (block.reply(proto::ReplyIndex{}).intersects(row_.request))
            unknown_.add();
        return proto::DiskAct::none();
    }
    if (*request == IdeRequest::None)
        return proto::DiskAct::none();

    if (*request == IdeRequest::Reset) {
        latch_hot_swap_(block);
        const IdeTaskFile presented = IdeTaskFile::decode(
            IdeTaskFile::image_of(block, row_.task_file_at)
        );
        selected_ = presented.drive;
        wire_.begin(row_.units.at(selected_));
        apply_armed_();
        engine_.reset(presented, wire_);
        engine_.idle(wire_);
    } else if (*request == IdeRequest::Command) {
        const IdeTaskFile presented = IdeTaskFile::decode(
            IdeTaskFile::image_of(block, row_.task_file_at)
        );
        selected_ = presented.drive;
        wire_.begin(row_.units.at(selected_));
        engine_.command(presented, wire_);
    } else {
        wire_.begin(row_.units.at(selected_));
        engine_.data_request(wire_);
    }
    open_ = true;
    const proto::DiskAct act = wire_.next(bytes);
    if (act.kind() == proto::DiskAct::Kind::None)
        open_ = false;
    return act;
}

proto::DiskAct IdeChannel::resume(
    std::span<std::byte, proto::kDiskActBytes> bytes,
    infra::OptRef<IRemountAsk> ask
) noexcept {
    TASTY_SEAT_BODY(IdeChannel);
    if (!open_)
        return proto::DiskAct::none();
    proto::DiskAct act = wire_.next(bytes);
    if (act.kind() != proto::DiskAct::Kind::None)
        return act;
    if (wire_.receive_done()) {
        wire_.received_taken();
        engine_.received(wire_, ask);
        act = wire_.next(bytes);
        if (act.kind() != proto::DiskAct::Kind::None)
            return act;
    }
    open_ = false;
    return proto::DiskAct::none();
}

void IdeChannel::release() noexcept {
    TASTY_SEAT_BODY(IdeChannel);
    engine_.forget();
    wire_.clear();
    open_ = false;
    configured_ = {};
    mounted_ = {};
    armed_ = {};
    tray_ = {};
    allow_ = {};
    last_.reset();
    presented_ = false;
}

bool IdeChannel::present(IdeDriveSelect drive) const noexcept {
    return engine_.present(drive);
}

infra::OptRef<AtapiDrive> IdeChannel::playing_drive() noexcept {
    return engine_.playing_drive();
}

svc::DiskGeometry IdeChannel::geometry(IdeDriveSelect drive) const noexcept {
    return engine_.geometry(drive);
}

}
