// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>

#include "cores/disk_servant.h"
#include "cores/minimig/floppy_write_capture.h"
#include "infra/indexed_array.h"
#include "infra/opt_ref.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "infra/tally.h"
#include "infra/xthread/wake_flag.h"
#include "proto/disk_act.h"
#include "proto/disk_status_tag.h"
#include "proto/types.h"
#include "proto/user_io/poll_frame.h"
#include "svc/adf_codec.h"
#include "svc/card_writer.h"
#include "svc/disk_format_row.h"
#include "svc/disk_image.h"

namespace mister::svc {
class Vfs;
}

namespace mister::cores::minimig {

class FloppyServant final : public IDiskServant {
    TASTY_SEAT_RESIDENT(Io);

public:
    static constexpr std::size_t kDrives = 4;

    FloppyServant(
        infra::OptRef<const svc::Vfs> vfs,
        std::span<const svc::DiskFormatRow> formats,
        infra::OptRef<xthread::WakeFlag> activity = {}
    ) noexcept;

    [[nodiscard]] MountState bind(proto::DiskUnit unit, std::string_view path) noexcept override;
    [[nodiscard]] proto::DiskAct start(
        const proto::user_io::PollFrame& block,
        std::span<std::byte, proto::kDiskActBytes> bytes
    ) noexcept override;
    [[nodiscard]] proto::DiskAct resume(
        std::span<std::byte, proto::kDiskActBytes> bytes
    ) noexcept override;
    void release() noexcept override;

    [[nodiscard]] proto::DiskAct standing() const noexcept override;

    [[nodiscard]] infra::Tally commands() const noexcept { return commands_; }

    [[nodiscard]] infra::Tally io_faults() const noexcept { return io_faults_; }

    [[nodiscard]] const FloppyWriteCapture& capture() const noexcept { return capture_; }

private:
    struct Drive {
        std::unique_ptr<svc::IDiskImage> image{};
        svc::AdfCodec::TrackCount tracks{};
        svc::AdfCodec::Track track{};

        std::optional<svc::AdfCodec::Track> track_prev{};
        svc::AdfCodec::Sector sector_offset{};
    };

    struct Read {
        proto::DiskUnit unit{};
        svc::AdfCodec::Track track{};
        svc::AdfCodec::Sector sector{};
        bool gap = false;
        proto::FileSize sent{};
        hal::LinkWord sync{};
        proto::DiskStatusTag tag{};
    };

    [[nodiscard]] proto::DiskAct read_(
        const Read& asked,
        std::span<std::byte, proto::kDiskActBytes> bytes
    ) noexcept;
    [[nodiscard]] proto::DiskAct next_piece_(
        std::span<std::byte, proto::kDiskActBytes> bytes
    ) noexcept;

    [[nodiscard]] proto::DiskAct piece_(
        bool opens,
        std::span<std::byte, proto::kDiskActBytes> bytes
    ) noexcept;
    void forget_command_() noexcept;

    infra::OptRef<const svc::Vfs> vfs_;
    std::optional<svc::CardWriter> card_;
    infra::OptRef<xthread::WakeFlag> activity_;
    std::span<const svc::DiskFormatRow> formats_;
    infra::IndexedArray<Drive, kDrives, proto::DiskUnit> drives_{};
    FloppyWriteCapture capture_{};

    std::optional<Read> reading_{};
    proto::FioWordCount in_owed_{};

    infra::Tally commands_{};
    infra::Tally io_faults_{};
};

}
