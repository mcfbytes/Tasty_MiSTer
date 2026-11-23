// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>

#include "cores/disk_servant.h"
#include "cores/mount_state.h"
#include "cores/x86/fdc_request.h"
#include "cores/x86/x86_bus_poll.h"
#include "infra/byte_offset.h"
#include "infra/indexed_array.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "infra/tally.h"
#include "infra/xthread/wake_flag.h"
#include "proto/disk_act.h"
#include "proto/disk_address.h"
#include "proto/disk_protocol_row.h"
#include "proto/types.h"
#include "proto/user_io/poll_frame.h"
#include "svc/card_writer.h"
#include "svc/disk_format_row.h"
#include "svc/disk_image.h"

namespace mister::svc {
class Vfs;
}

namespace mister::cores::x86 {

class FloppyServant final : public IDiskServant {
    TASTY_SEAT_RESIDENT(Io);

public:
    static constexpr proto::BlockSize kSector{512};

    static constexpr proto::BlockCount kMaxWriteSectors{16};

    static constexpr proto::DiskAddress kDataPort{0xF2FF};

    static constexpr infra::ByteCount kActPayload{
        proto::kDiskActBytes / proto::DiskAct::kWordBytes
    };

    FloppyServant(
        infra::OptRef<const svc::Vfs> vfs,
        std::span<const svc::DiskFormatRow> formats,
        proto::DiskUnits drives,
        proto::ReplyIndex params_at,
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

    [[nodiscard]] infra::Tally commands() const noexcept { return commands_; }

    [[nodiscard]] infra::Tally refused_counts() const noexcept { return refused_counts_; }

    [[nodiscard]] infra::Tally io_faults() const noexcept { return io_faults_; }

private:
    struct Transfer {
        FdcRequest request{};
        infra::ByteOffset at{};
        infra::ByteCount total{};
    };

    [[nodiscard]] infra::OptRef<svc::IDiskImage> image_(proto::DiskUnit drive) noexcept;
    [[nodiscard]] proto::DiskAct start_read_(
        const FdcRequest& r,
        std::span<std::byte, proto::kDiskActBytes> bytes
    ) noexcept;
    [[nodiscard]] proto::DiskAct start_write_(const FdcRequest& r) noexcept;

    [[nodiscard]] proto::DiskAct data_act_(
        std::span<std::byte, proto::kDiskActBytes> bytes
    ) noexcept;
    void take_written_(std::span<const std::byte, proto::kDiskActBytes> bytes) noexcept;
    void commit_write_() noexcept;

    infra::OptRef<const svc::Vfs> vfs_;
    std::optional<svc::CardWriter> card_;
    infra::OptRef<xthread::WakeFlag> activity_;
    std::span<const svc::DiskFormatRow> formats_;
    proto::DiskUnits drives_;
    proto::ReplyIndex params_at_;
    infra::IndexedArray<std::unique_ptr<svc::IDiskImage>, kFdcDrives, proto::DiskUnit>
        image_slot_{};
    std::optional<Transfer> transfer_{};

    std::array<std::byte, kSector.window_bytes(kMaxWriteSectors)> buffer_{};
    infra::Tally commands_{};
    infra::Tally refused_counts_{};
    infra::Tally io_faults_{};
};

}
