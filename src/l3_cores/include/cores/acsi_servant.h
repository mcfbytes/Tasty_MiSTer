// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>
#include <variant>

#include "cores/disk_servant.h"
#include "cores/scsi_cdb.h"
#include "cores/types.h"
#include "infra/error.h"
#include "infra/fixed_str.h"
#include "infra/indexed_array.h"
#include "infra/opt_ref.h"
#include "infra/wake_flag.h"
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
    static constexpr proto::BlockSize kBlock{512};
    static constexpr proto::BlockCount kChunkBlocks{128};
    static constexpr std::size_t kChunkBytes = kBlock.length_of(kChunkBlocks);
    static constexpr std::size_t kReplyBytes = 512;
    static constexpr std::size_t kInquiryNameBytes = 11;
    static constexpr std::size_t kInquirySerialBytes = 8;
    static_assert(kReplyBytes <= proto::kDiskActBytes && kChunkBytes % proto::kDiskActBytes == 0);

    AcsiServant(
        infra::OptRef<const svc::Vfs> vfs,
        std::span<const svc::DiskFormatRow> formats,
        std::string_view serial,
        infra::OptRef<xthread::WakeFlag> activity = {}
    ) noexcept;

    [[nodiscard]] MountState bind(proto::DiskUnit unit, std::string_view path) noexcept override;
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
        infra::OptRef<svc::IDiskImage> img,
        std::string_view name
    ) noexcept;

    [[nodiscard]] std::uint32_t commands() const noexcept { return commands_; }

    [[nodiscard]] std::uint32_t naks() const noexcept { return naks_; }

    [[nodiscard]] std::uint32_t io_faults() const noexcept { return io_faults_; }

private:
    struct HeldImage {
        TASTY_SEAT_EXEMPT(component);
        using Owned = std::unique_ptr<svc::IDiskImage>;
        using Borrowed = infra::OptRef<svc::IDiskImage>;

        enum class HeldKind : std::uint8_t { Empty, Owned, Borrowed, kCount };
        using Held = std::variant<std::monostate, Owned, Borrowed>;
        static_assert(
            std::variant_size_v<Held> == static_cast<std::size_t>(HeldKind::kCount),
            "HeldImage: one HeldKind name per arm"
        );
        template <HeldKind K, class T>
        static constexpr bool kArmIs = std::
            is_same_v<std::variant_alternative_t<static_cast<std::size_t>(K), Held>, T>;
        static_assert(
            kArmIs<HeldKind::Empty, std::monostate>
            && kArmIs<HeldKind::Owned, Owned>
            && kArmIs<HeldKind::Borrowed, Borrowed>
        );
        Held held{};

        [[nodiscard]] infra::OptRef<svc::IDiskImage> get() const noexcept {
            if (const auto* owned = std::get_if<Owned>(&held))
                return infra::opt_ref_of(owned->get());
            if (const auto* lent = std::get_if<Borrowed>(&held))
                return *lent;
            return {};
        }

        void hold(Owned img) noexcept {
            if (img)
                held = std::move(img);
            else
                held = std::monostate{};
        }

        void borrow(infra::OptRef<svc::IDiskImage> img) noexcept {
            if (img)
                held = Borrowed{img};
            else
                held = std::monostate{};
        }

        void clear() noexcept { held = std::monostate{}; }
    };

    struct Slot {
        HeldImage image{};
        FixedStr<kInquiryNameBytes + 1, StrFit::Clip> name{};
        SenseCode asc = SenseCode::None;
    };

    struct ChunkWindow {
        std::size_t len = 0;
        std::size_t off = 0;
    };

    struct ReadTransfer {
        proto::Lba lba{};
        proto::BlockCount length{};
    };

    struct WriteTransfer {
        proto::Lba lba{};
        proto::BlockCount length{};
        proto::BlockCount chunk_blocks{};
        std::size_t in_len = 0;
    };

    enum class TransferKind : std::uint8_t { None, Read, Write, kCount };
    using Transfer = std::variant<std::monostate, ReadTransfer, WriteTransfer>;
    static constexpr std::size_t kTransferArms = static_cast<std::size_t>(TransferKind::kCount);
    static_assert(
        std::variant_size_v<Transfer> == kTransferArms,
        "Command: one TransferKind name per arm"
    );
    template <TransferKind K, class T>
    static constexpr bool kTransferArmIs = std::
        is_same_v<std::variant_alternative_t<static_cast<std::size_t>(K), Transfer>, T>;
    static_assert(
        kTransferArmIs<TransferKind::None, std::monostate>
        && kTransferArmIs<TransferKind::Read, ReadTransfer>
        && kTransferArmIs<TransferKind::Write, WriteTransfer>
    );

    struct Command {
        Transfer transfer{};
        ChunkWindow chunk{};
        std::size_t reply_len = 0;
        bool reply_owed = false;
        ScsiStatus status = ScsiStatus::Good;
        bool ack_owed = false;
        bool nak_owed = false;
    };

    void dispatch_(Slot& slot, const std::optional<Cdb>& cdb);

    void answer_ready_(Slot& slot, const Cdb& cdb);
    void answer_read_capacity_(Slot& slot, const Cdb& cdb);
    void answer_sense_(Slot& slot, const Cdb& cdb);
    void answer_inquiry_(Slot& slot, const Cdb& cdb);
    void answer_mode_sense_(Slot& slot, const Cdb& cdb);
    void answer_blocks_(Slot& slot, const Cdb& cdb);
    void answer_(Slot& slot, ScsiStatus status, SenseCode asc);
    void read_blocks_(Slot& slot, const Cdb& cdb);
    void write_blocks_(Slot& slot, const Cdb& cdb);

    [[nodiscard]] std::optional<proto::DiskAct> read_step_(
        ReadTransfer& arm,
        std::span<std::byte, proto::kDiskActBytes> bytes
    );
    [[nodiscard]] std::optional<proto::DiskAct> write_step_(
        WriteTransfer& arm,
        std::span<std::byte, proto::kDiskActBytes> bytes
    );

    [[nodiscard]] bool write_take_in_(WriteTransfer& arm, std::span<const std::byte> bytes);

    void memory_write_(std::span<const std::byte> src);
    void ack_(ScsiStatus status);
    void nak_();

    [[nodiscard]] proto::BlockCount block_count_(const Slot& slot) const noexcept;

    [[nodiscard]] static svc::IDiskImage& image_of_(const Slot& slot) noexcept;

    [[nodiscard]] static constexpr std::uint64_t image_offset(proto::Lba lba) noexcept {
        return kBlock.position_of(lba);
    }

    void forget_command_() noexcept;
    [[nodiscard]] proto::DiskAct next_act_(std::span<std::byte, proto::kDiskActBytes> bytes);

    infra::OptRef<const svc::Vfs> vfs_;
    infra::OptRef<xthread::WakeFlag> activity_;
    std::span<const svc::DiskFormatRow> formats_;
    infra::IndexedArray<Slot, kTargets, AcsiTarget> slot_{};
    FixedStr<kInquirySerialBytes + 1, StrFit::Clip> serial_{};
    std::array<std::byte, kReplyBytes> reply_{};
    std::array<std::byte, kChunkBytes> chunk_{};

    AcsiTarget target_{};
    Command cmd_{};

    std::uint32_t commands_ = 0;
    std::uint32_t naks_ = 0;
    std::uint32_t io_faults_ = 0;
};

}
