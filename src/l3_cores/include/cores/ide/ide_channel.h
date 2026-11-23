// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "cores/ide/atapi_drive.h"
#include "cores/ide/ide_act_wire.h"
#include "cores/ide/ide_channel_row.h"
#include "cores/ide/ide_drive_pair.h"
#include "cores/ide/ide_engine.h"
#include "cores/ide/ide_geometry_rule.h"
#include "cores/ide/ide_remount_ask.h"
#include "cores/ide/ide_media.h"
#include "cores/ide/ide_request.h"
#include "cores/ide/ide_task_file.h"
#include "cores/ide/ide_tray.h"
#include "cores/ide/ide_types.h"
#include "infra/opt_ref.h"
#include "infra/path_text.h"
#include "infra/seat.h"
#include "infra/tally.h"
#include "proto/disk_act.h"
#include "proto/types.h"
#include "proto/user_io/poll_frame.h"
#include "svc/card_writer.h"
#include "svc/disk_format_row.h"
#include "svc/disk_geometry.h"
#include "svc/vhd_sidecar.h"

namespace mister::svc {
class Vfs;
}

namespace mister::cores::ide {

class IdeChannel {
    TASTY_SEAT_RESIDENT(Io);

public:
    IdeChannel(
        const IdeChannelRow& row,
        infra::OptRef<const svc::Vfs> vfs,
        std::span<const svc::DiskFormatRow> formats,
        const IGeometryRule& geometry
    ) noexcept;

    void configure(IdeDriveSelect drive, std::string_view path) noexcept;

    [[nodiscard]] std::optional<IdeRequest> asks(
        const proto::user_io::PollFrame& block
    ) const noexcept;

    [[nodiscard]] proto::DiskAct start(
        const proto::user_io::PollFrame& block,
        std::span<std::byte, proto::kDiskActBytes> bytes
    ) noexcept;

    [[nodiscard]] proto::DiskAct resume(
        std::span<std::byte, proto::kDiskActBytes> bytes,
        infra::OptRef<IRemountAsk> ask = {}
    ) noexcept;

    void release() noexcept;

    [[nodiscard]] bool present(IdeDriveSelect drive) const noexcept;
    [[nodiscard]] svc::DiskGeometry geometry(IdeDriveSelect drive) const noexcept;

    [[nodiscard]] infra::OptRef<AtapiDrive> playing_drive() noexcept;

    [[nodiscard]] const IdeChannelRow& row() const noexcept { return row_; }

    [[nodiscard]] Tray tray(IdeDriveSelect drive) const noexcept { return tray_.at(drive); }

    [[nodiscard]] infra::Tally unknown_requests() const noexcept { return unknown_; }

    [[nodiscard]] infra::Tally refused_mounts() const noexcept { return refused_mounts_; }

    [[nodiscard]] infra::Tally io_faults() const noexcept { return io_faults_; }

private:
    static constexpr proto::FileSize kSidecarBytes{4096};

    enum class MountWhen : std::uint8_t { Reset, AtOnce };

    void mount_(IdeDriveSelect drive, MountWhen when) noexcept;
    void mount_disk_(IdeDriveSelect drive) noexcept;
    void mount_disc_(IdeDriveSelect drive, MountWhen when) noexcept;

    void hot_mount_(IdeDriveSelect drive) noexcept;
    void apply_armed_() noexcept;
    void latch_hot_swap_(const proto::user_io::PollFrame& block) noexcept;

    [[nodiscard]] bool hot_swap_allowed_(IdeDriveSelect drive) const noexcept {
        return allow_.at(drive);
    }

    [[nodiscard]] Tray empty_tray_(IdeDriveSelect drive) const noexcept {
        return hot_swap_allowed_(drive) ? Tray::Empty : Tray::Fixed;
    }

    void serve_empty_(IdeDriveSelect drive) noexcept;
    [[nodiscard]] std::optional<svc::VhdSidecar> sidecar_of_(std::string_view path) const noexcept;

    const IdeChannelRow& row_;
    infra::OptRef<const svc::Vfs> vfs_;
    std::optional<svc::CardWriter> card_;
    std::span<const svc::DiskFormatRow> formats_;
    const IGeometryRule& geometry_;
    DrivePair<infra::PathText> configured_{};
    DrivePair<infra::PathText> mounted_{};
    DrivePair<bool> armed_{};
    DrivePair<Tray> tray_{};
    DrivePair<bool> allow_{};
    std::optional<IdeRequest> last_{};
    bool presented_ = false;
    IdeDriveSelect selected_ = IdeDriveSelect::Master;
    IdeEngine engine_{};
    IdeActWire wire_;
    bool open_ = false;
    infra::Tally unknown_{};
    infra::Tally refused_mounts_{};
    infra::Tally io_faults_{};
};

}
