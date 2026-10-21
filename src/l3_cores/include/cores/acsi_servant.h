// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

#include "cores/disk_servant.h"
#include "cores/types.h"
#include "infra/error.h"
#include "infra/fixed_str.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "proto/disk_act.h"
#include "svc/disk_format_row.h"
#include "svc/disk_image.h"

namespace mister::svc {
class Vfs;
}

namespace mister::cores {

class AcsiServant final : public IDiskServant {
    TASTY_SEAT_RESIDENT(Io);

public:
    static constexpr std::size_t kTargets = 2;
    static constexpr std::uint32_t kBlockBytes = 512;
    static constexpr std::uint32_t kChunkBlocks = 128;
    static constexpr std::size_t kChunkBytes = kChunkBlocks * kBlockBytes;
    static constexpr std::size_t kReplyBytes = 512;
    static constexpr std::size_t kInquiryNameBytes = 11;
    static constexpr std::size_t kInquirySerialBytes = 8;
    static_assert(kReplyBytes <= proto::kDiskActBytes && kChunkBytes % proto::kDiskActBytes == 0);

    AcsiServant(
        infra::OptRef<const svc::Vfs> vfs,
        std::span<const svc::DiskFormatRow> formats,
        std::string_view serial
    ) noexcept;

    [[nodiscard]] MountState bind(std::uint8_t unit, std::string_view path) noexcept override;
    [[nodiscard]] proto::DiskAct start(
        std::span<const std::byte, proto::kDiskStateBytes> state,
        std::span<std::byte, proto::kDiskActBytes> bytes
    ) noexcept override;
    [[nodiscard]] proto::DiskAct resume(
        std::span<std::byte, proto::kDiskActBytes> bytes
    ) noexcept override;
    void release() noexcept override;

    [[nodiscard]] Ex<void> bind_target(
        AcsiTarget t,
        svc::IDiskImage* img,
        std::string_view name
    ) noexcept;

    [[nodiscard]] std::uint32_t commands() const noexcept { return commands_; }

    [[nodiscard]] std::uint32_t naks() const noexcept { return naks_; }

    [[nodiscard]] std::uint32_t io_faults() const noexcept { return io_faults_; }

private:
    struct Slot {
        infra::OptRef<svc::IDiskImage> image{};
        FixedStr<kInquiryNameBytes + 1, StrFit::Clip> name{};
        std::uint8_t asc = 0;
    };

    enum class Transfer : std::uint8_t { None, Read, Write };

    void dispatch_(Slot& slot);
    void answer_(Slot& slot, std::uint8_t status, std::uint8_t asc);
    void read_blocks_(Slot& slot, std::uint64_t lba, std::uint32_t length);
    void write_blocks_(Slot& slot, std::uint64_t lba, std::uint32_t length);

    void memory_write_(std::span<const std::byte> src);
    void ack_(std::uint8_t status);
    void nak_();

    void forget_command_() noexcept;
    [[nodiscard]] proto::DiskAct next_act_(std::span<std::byte, proto::kDiskActBytes> bytes);
    [[nodiscard]] proto::DiskAct read_step_(std::span<std::byte, proto::kDiskActBytes> bytes);
    [[nodiscard]] proto::DiskAct write_step_(std::span<std::byte, proto::kDiskActBytes> bytes);

    infra::OptRef<const svc::Vfs> vfs_;
    std::span<const svc::DiskFormatRow> formats_;
    std::array<std::unique_ptr<svc::IDiskImage>, kTargets> owned_{};
    std::array<Slot, kTargets> slot_{};
    FixedStr<kInquirySerialBytes + 1, StrFit::Clip> serial_{};
    std::array<std::byte, proto::kDiskStateBytes> state_{};
    std::array<std::byte, kReplyBytes> reply_{};
    std::array<std::byte, kChunkBytes> chunk_{};

    std::uint8_t target_ = 0;
    Transfer transfer_ = Transfer::None;
    std::uint64_t lba_ = 0;
    std::uint32_t length_ = 0;
    std::uint32_t chunk_blocks_ = 0;
    std::size_t chunk_len_ = 0;
    std::size_t chunk_off_ = 0;
    std::size_t reply_len_ = 0;
    bool reply_owed_ = false;
    std::size_t in_len_ = 0;
    std::uint8_t status_ = 0;
    bool ack_owed_ = false;
    bool nak_owed_ = false;

    std::uint32_t commands_ = 0;
    std::uint32_t naks_ = 0;
    std::uint32_t io_faults_ = 0;
};

}
