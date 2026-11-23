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

#include "cores/ide/atapi_disc.h"
#include "cores/ide/atapi_drive.h"
#include "cores/ide/atapi_identify.h"
#include "cores/ide/atapi_packet.h"
#include "cores/ide/ide_command.h"
#include "cores/ide/ide_identify.h"
#include "cores/ide/ide_remount_ask.h"
#include "cores/ide/ide_media.h"
#include "cores/ide/ide_task_file.h"
#include "cores/ide/ide_transport.h"
#include "cores/ide/ide_types.h"
#include "infra/opt_ref.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "infra/tally.h"
#include "proto/types.h"
#include "svc/disk_geometry.h"
#include "svc/disk_image.h"

namespace mister::cores::ide {

class IdeEngine {
    TASTY_SEAT_RESIDENT(Io);

public:
    static constexpr proto::BlockCount kMostBurst{32};
    static constexpr proto::BlockCount kDefaultBurst{16};

    struct Mount {
        std::unique_ptr<svc::IDiskImage> image{};
        IdeMedia media{};
        std::string_view name{};
    };

    struct DiscMount {
        std::unique_ptr<AtapiDisc> disc{};
        std::string_view name{};
    };

    IdeEngine() noexcept = default;
    IdeEngine(const IdeEngine&) = delete;
    IdeEngine& operator=(const IdeEngine&) = delete;

    void insert(IdeDriveSelect drive, Mount mount, IIdeTransport& wire) noexcept;
    void insert_disc(IdeDriveSelect drive, DiscMount mount, IIdeTransport& wire) noexcept;
    void eject(IdeDriveSelect drive, IIdeTransport& wire) noexcept;

    void forget() noexcept;

    void idle(IIdeTransport& wire) noexcept;

    void command(const IdeTaskFile& presented, IIdeTransport& wire) noexcept;

    void data_request(IIdeTransport& wire) noexcept;

    void reset(const IdeTaskFile& presented, IIdeTransport& wire) noexcept;

    void received(IIdeTransport& wire, infra::OptRef<IRemountAsk> ask = {}) noexcept;

    [[nodiscard]] bool present(IdeDriveSelect drive) const noexcept;
    [[nodiscard]] svc::DiskGeometry geometry(IdeDriveSelect drive) const noexcept;

    [[nodiscard]] infra::OptRef<AtapiDrive> playing_drive() noexcept;
    [[nodiscard]] bool transfer_open() const noexcept;

    [[nodiscard]] infra::Tally refusals() const noexcept { return refusals_; }

    [[nodiscard]] infra::Tally io_faults() const noexcept { return io_faults_; }

private:
    enum class Phase : std::uint8_t {
        Idle,
        ReadyOwed,
        Reading,
        WriteReady,
        WriteReceiving,
        PacketCommand,
        PacketDataIn,
        PacketDataOut
    };

    enum class Bursts : std::uint8_t { Single, Multiple };
    enum class Sink : std::uint8_t { Image, RemountName };

    struct NoDrive {};

    struct DiskDrive {
        std::unique_ptr<svc::IDiskImage> image{};
        svc::DiskGeometry geometry{};
        proto::BlockCount burst = kDefaultBurst;
        IdeIdentify identify{};
    };

    struct PacketDrive {
        AtapiDrive drive{};
        std::unique_ptr<AtapiDisc> disc{};
        std::array<std::byte, kAtapiIdentifyBytes.size()> identify{};
    };

    enum class DriveKind : std::uint8_t { NoDrive, Disk, Packet, kCount };
    using Drive = std::variant<NoDrive, DiskDrive, PacketDrive>;
    static_assert(
        std::variant_size_v<Drive> == infra::ordinal(DriveKind::kCount),
        "IdeEngine::Drive: one Kind name per arm"
    );
    template <DriveKind K, class T>
    static constexpr bool
        kArmIs = std::is_same_v<std::variant_alternative_t<infra::ordinal(K), Drive>, T>;
    static_assert(
        kArmIs<DriveKind::NoDrive, NoDrive>
        && kArmIs<DriveKind::Disk, DiskDrive>
        && kArmIs<DriveKind::Packet, PacketDrive>
    );

    struct Transfer {
        proto::Lba next{};
        proto::BlockCount burst{};
        Bursts bursts = Bursts::Single;
        Sink sink = Sink::Image;
        bool lost = false;
    };

    using Buffer = std::array<std::byte, svc::DiskGeometry::kSector.window_bytes(kMostBurst)>;
    using PacketBuffer = std::array<std::byte, AtapiDrive::kParameterBytes.size()>;

    [[nodiscard]] static DriveKind kind_of(const Drive& drive) noexcept {
        if (std::get_if<NoDrive>(&drive) != nullptr)
            return DriveKind::NoDrive;
        if (std::get_if<DiskDrive>(&drive) != nullptr)
            return DriveKind::Disk;
        return DriveKind::Packet;
    }

    [[nodiscard]] Drive& drive_(IdeDriveSelect which) noexcept {
        return which == IdeDriveSelect::Slave ? slave_ : master_;
    }

    [[nodiscard]] const Drive& drive_(IdeDriveSelect which) const noexcept {
        return which == IdeDriveSelect::Slave ? slave_ : master_;
    }

    [[nodiscard]] Drive& selected_() noexcept { return drive_(regs_.drive); }

    [[nodiscard]] DriveKind selected_kind_() const noexcept { return kind_of(drive_(regs_.drive)); }

    [[nodiscard]] svc::DiskGeometry selected_geometry_() const noexcept {
        if (const auto* disk = std::get_if<DiskDrive>(&drive_(regs_.drive)))
            return disk->geometry;
        return {};
    }

    [[nodiscard]] DiskDrive& selected_disk_() noexcept {
        DiskDrive* disk = std::get_if<DiskDrive>(&selected_());
        if (disk == nullptr)
            fatal(Error{Errc::contract, ERR_SITE(), 0}, "IdeEngine: no disk");
        return *disk;
    }

    [[nodiscard]] PacketDrive& selected_packet_() noexcept {
        PacketDrive* packet = std::get_if<PacketDrive>(&selected_());
        if (packet == nullptr)
            fatal(Error{Errc::contract, ERR_SITE(), 0}, "IdeEngine: no packet drive");
        return *packet;
    }

    void take_(const IdeTaskFile& presented) noexcept;

    [[nodiscard]] bool run_(IdeCommand command, IIdeTransport& wire) noexcept;
    [[nodiscard]] bool run_disk_(IdeCommand command, IIdeTransport& wire) noexcept;
    [[nodiscard]] bool run_packet_(IdeCommand command, IIdeTransport& wire) noexcept;
    void packet_step_(const AtapiStep& step, IIdeTransport& wire) noexcept;

    void deliver_name_(infra::OptRef<IRemountAsk> ask) noexcept;
    void write_regs_(IIdeTransport& wire) noexcept;
    void complete_(IdeStatus status, IIdeTransport& wire) noexcept;
    void abort_(IIdeTransport& wire) noexcept;
    void announce_(IdeDriveSelect drive, IIdeTransport& wire) noexcept;

    void identify_(IIdeTransport& wire) noexcept;
    void begin_read_(Bursts bursts, IIdeTransport& wire) noexcept;
    void send_burst_(IIdeTransport& wire) noexcept;
    void begin_write_(Bursts bursts, Sink sink, IIdeTransport& wire) noexcept;
    void ask_burst_(IdeStatus irq, IIdeTransport& wire) noexcept;
    void keep_burst_() noexcept;

    [[nodiscard]] proto::Lba block_() noexcept;

    void park_before_(proto::Lba next) noexcept;
    [[nodiscard]] proto::BlockCount next_burst_() noexcept;

    [[nodiscard]] proto::BlockCount drive_burst_() noexcept { return selected_disk_().burst; }

    [[nodiscard]] std::span<std::byte> burst_bytes_() noexcept;

    Drive master_{};
    Drive slave_{};
    IdeTaskFile regs_{};
    Phase phase_ = Phase::Idle;
    Transfer transfer_{};
    Buffer buffer_{};
    PacketBuffer packet_buffer_{};

    infra::ByteCount packet_out_{};
    infra::Tally refusals_{};
    infra::Tally io_faults_{};
};

}
