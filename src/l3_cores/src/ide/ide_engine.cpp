// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/ide/ide_engine.h"

#include <algorithm>
#include <array>
#include <utility>

#include "cores/ide/atapi_step.h"
#include "infra/byte_view.h"
#include "infra/path_text.h"
#include "infra/span_cut.h"

namespace mister::cores::ide {

namespace {

using Fault = IdeTaskFile::Fault;

constexpr proto::BlockSize kSector = svc::DiskGeometry::kSector;
constexpr proto::BlockCount kOneSector{1};
constexpr std::array<IdeDriveSelect, 2> kDrives{IdeDriveSelect::Master, IdeDriveSelect::Slave};

constexpr IdeStatus kDone = IdeStatus::kReady | IdeStatus::kIrq;
constexpr IdeStatus kDoneSettled = kDone | IdeStatus::kSeekComplete;
constexpr IdeStatus kAborted = kDone | IdeStatus::kError;
constexpr IdeStatus kBurstOut = kDone | IdeStatus::kDataRequest | IdeStatus::kFastRead;
constexpr IdeStatus kBurstWanted = IdeStatus::kReady | IdeStatus::kDataRequest;
constexpr IdeStatus kIdentified = kDone | IdeStatus::kDataRequest | IdeStatus::kLastRead;

constexpr IdeStatus kPacketBurst = kBurstWanted | IdeStatus::kIrq;
constexpr IdeStatus kPacketIdentified = kPacketBurst | IdeStatus::kLastRead;

constexpr IdeStatus kUnsettled = IdeStatus::kBusy | IdeStatus::kError;

}

void IdeEngine::insert(IdeDriveSelect which, Mount mount, IIdeTransport& wire) noexcept {
    TASTY_SEAT_BODY(IdeEngine);
    Drive& slot = drive_(which);
    if (!mount.image) {
        slot.emplace<NoDrive>();
    } else {
        DiskDrive disk{};
        disk.identify = IdeIdentify::of(mount.media, mount.name);

        disk.geometry = mount.media.geometry.addressing(mount.image->size());
        disk.image = std::move(mount.image);
        slot.emplace<DiskDrive>(std::move(disk));
    }
    announce_(which, wire);
}

void IdeEngine::insert_disc(IdeDriveSelect which, DiscMount mount, IIdeTransport& wire) noexcept {
    TASTY_SEAT_BODY(IdeEngine);
    Drive& slot = drive_(which);
    PacketDrive* held = std::get_if<PacketDrive>(&slot);
    PacketDrive& packet = held != nullptr ? *held : slot.emplace<PacketDrive>();

    packet.drive.remove();
    packet.disc = std::move(mount.disc);
    packet.identify = atapi_identify(mount.name);
    if (packet.disc)
        packet.drive.insert(AtapiDrive::Disc{packet.disc->toc(), *packet.disc});
    announce_(which, wire);
}

void IdeEngine::eject(IdeDriveSelect which, IIdeTransport& wire) noexcept {
    TASTY_SEAT_BODY(IdeEngine);
    drive_(which).emplace<NoDrive>();
    announce_(which, wire);
}

void IdeEngine::forget() noexcept {
    TASTY_SEAT_BODY(IdeEngine);
    master_.emplace<NoDrive>();
    slave_.emplace<NoDrive>();
    regs_ = IdeTaskFile{};
    phase_ = Phase::Idle;
    transfer_ = Transfer{};
    packet_out_ = {};
}

void IdeEngine::announce_(IdeDriveSelect which, IIdeTransport& wire) noexcept {
    transfer_ = Transfer{};
    phase_ = Phase::ReadyOwed;
    wire.write_reg(IdeRegister::kPresence, presence_word(which, present(which)));
    wire.write_reg(IdeRegister::kPresence, no_wait_word());
}

bool IdeEngine::present(IdeDriveSelect which) const noexcept {
    return kind_of(drive_(which)) != DriveKind::NoDrive;
}

svc::DiskGeometry IdeEngine::geometry(IdeDriveSelect which) const noexcept {
    if (const auto* disk = std::get_if<DiskDrive>(&drive_(which)))
        return disk->geometry;
    return {};
}

infra::OptRef<AtapiDrive> IdeEngine::playing_drive() noexcept {
    for (const IdeDriveSelect which : kDrives) {
        PacketDrive* packet = std::get_if<PacketDrive>(&drive_(which));
        if (packet != nullptr && packet->drive.playing())
            return infra::opt_ref_of(&packet->drive);
    }
    return {};
}

bool IdeEngine::transfer_open() const noexcept {
    switch (phase_) {
        case Phase::Reading:
        case Phase::WriteReady:
        case Phase::WriteReceiving:
        case Phase::PacketCommand:
        case Phase::PacketDataIn:
        case Phase::PacketDataOut:
            return true;
        case Phase::Idle:
        case Phase::ReadyOwed:
            return false;
    }
    return false;
}

void IdeEngine::idle(IIdeTransport& wire) noexcept {
    TASTY_SEAT_BODY(IdeEngine);
    if (phase_ != Phase::ReadyOwed)
        return;
    phase_ = Phase::Idle;
    complete_(IdeStatus::kReady, wire);
}

void IdeEngine::command(const IdeTaskFile& presented, IIdeTransport& wire) noexcept {
    TASTY_SEAT_BODY(IdeEngine);
    phase_ = Phase::Idle;
    transfer_ = Transfer{};
    take_(presented);

    const bool served = regs_.command.has_value() && run_(*regs_.command, wire);
    if (!served)
        abort_(wire);
}

void IdeEngine::data_request(IIdeTransport& wire) noexcept {
    TASTY_SEAT_BODY(IdeEngine);
    switch (phase_) {
        case Phase::Reading:
            send_burst_(wire);
            return;
        case Phase::WriteReady:
            phase_ = Phase::WriteReceiving;
            wire.receive_data(burst_bytes_());
            return;
        case Phase::PacketCommand:

            wire.receive_data(
                infra::affine::first(std::span<std::byte>{packet_buffer_}, AtapiPacket::kBytes)
            );
            return;
        case Phase::PacketDataIn:
            packet_step_(selected_packet_().drive.data_in_done(), wire);
            return;
        case Phase::PacketDataOut:

            wire.receive_data(
                infra::affine::first_clamped(
                    std::span<std::byte>{packet_buffer_},
                    PacketWords::of(packet_out_).bytes()
                )
            );
            return;
        case Phase::Idle:
        case Phase::ReadyOwed:
        case Phase::WriteReceiving:
            break;
    }

    phase_ = Phase::Idle;
    transfer_ = Transfer{};
    abort_(wire);
}

void IdeEngine::reset(const IdeTaskFile& presented, IIdeTransport& wire) noexcept {
    TASTY_SEAT_BODY(IdeEngine);
    transfer_ = Transfer{};
    take_(presented);

    for (const IdeDriveSelect which : kDrives)
        if (auto* packet = std::get_if<PacketDrive>(&drive_(which)))
            packet->drive.bus_reset();
    switch (selected_kind_()) {
        case DriveKind::Packet:
            regs_.address = IdeAddress::packet_signature();
            regs_.fault = Fault::DiagnosticPassed;
            break;
        case DriveKind::Disk:
            regs_.address = IdeAddress::reset_signature(true);
            break;
        case DriveKind::NoDrive:
            regs_.address = IdeAddress::reset_signature(false);
            break;
        case DriveKind::kCount:
            break;
    }

    regs_.count = InterruptReason::kCommand.count();
    complete_(IdeStatus::kBusy, wire);
    phase_ = Phase::ReadyOwed;
}

void IdeEngine::received(IIdeTransport& wire, infra::OptRef<IRemountAsk> ask) noexcept {
    TASTY_SEAT_BODY(IdeEngine);
    switch (phase_) {
        case Phase::WriteReceiving:
            break;
        case Phase::PacketCommand: {
            const auto bytes = std::span<const std::byte>{packet_buffer_}
                                   .first<AtapiPacket::kBytes.size()>();
            packet_step_(selected_packet_().drive.packet(AtapiPacket::of(bytes)), wire);
            return;
        }
        case Phase::PacketDataOut: {
            const auto list = infra::affine::first_clamped(
                std::span<const std::byte>{packet_buffer_},
                PacketWords::of(packet_out_).bytes()
            );
            packet_step_(selected_packet_().drive.data_out(list), wire);
            return;
        }
        case Phase::Idle:
        case Phase::ReadyOwed:
        case Phase::Reading:
        case Phase::WriteReady:
        case Phase::PacketDataIn:
            return;
    }
    switch (transfer_.sink) {
        case Sink::RemountName:
            deliver_name_(ask);
            regs_.count = IdeSectorCount{};
            break;
        case Sink::Image:
            keep_burst_();
            break;
    }
    if (regs_.count.none()) {
        phase_ = Phase::Idle;
        complete_(kDone, wire);
        return;
    }
    ask_burst_(IdeStatus::kIrq, wire);
}

void IdeEngine::deliver_name_(infra::OptRef<IRemountAsk> ask) noexcept {
    static_assert(
        svc::DiskGeometry::kSector.block_bytes() <= infra::kPathMax,
        "one sector's bytes fit the remount name's cap"
    );
    if (!ask)
        return;
    const std::string_view sector = infra::as_text(burst_bytes_());
    ask->asked(regs_.address.remount_target(), regs_.drive, sector.substr(0, sector.find('\0')));
}

void IdeEngine::take_(const IdeTaskFile& presented) noexcept {
    const proto::BlockCount burst = regs_.burst;
    regs_ = presented;
    regs_.burst = burst;
    regs_.fault = Fault::None;
    regs_.status = IdeStatus{};
}

void IdeEngine::write_regs_(IIdeTransport& wire) noexcept {

    if (selected_kind_() != DriveKind::Packet) {
        if (!regs_.status.intersects(kUnsettled))
            regs_.status = regs_.status | IdeStatus::kSeekComplete;
    } else if (!regs_.packet) {

        regs_.packet = PacketWords{};
    }
    wire.write_task_file(regs_);
}

void IdeEngine::complete_(IdeStatus status, IIdeTransport& wire) noexcept {
    regs_.status = status;
    write_regs_(wire);
}

void IdeEngine::abort_(IIdeTransport& wire) noexcept {
    refusals_.add();
    regs_.fault = Fault::Aborted;
    complete_(kAborted, wire);
}

proto::Lba IdeEngine::block_() noexcept {
    return regs_.lba ? regs_.address.lba() : regs_.address.chs_lba(selected_geometry_());
}

void IdeEngine::park_before_(proto::Lba next) noexcept {
    const proto::Lba last = next.wrapping_prev();
    regs_.address = regs_.lba ? IdeAddress::of_lba(last)
                              : IdeAddress::of_chs(last, selected_disk_().geometry);
}

proto::BlockCount IdeEngine::next_burst_() noexcept {
    switch (transfer_.bursts) {
        case Bursts::Single:
            return kOneSector;
        case Bursts::Multiple:
            return regs_.count.burst(drive_burst_());
    }
    return kOneSector;
}

std::span<std::byte> IdeEngine::burst_bytes_() noexcept {
    return infra::affine::first(std::span<std::byte>(buffer_), kSector.bytes_of(transfer_.burst));
}

bool IdeEngine::run_(IdeCommand command, IIdeTransport& wire) noexcept {

    if (command == IdeCommand::MountImage) {
        begin_write_(Bursts::Single, Sink::RemountName, wire);
        return true;
    }
    switch (selected_kind_()) {
        case DriveKind::NoDrive:
            return false;
        case DriveKind::Disk:
            return run_disk_(command, wire);
        case DriveKind::Packet:
            return run_packet_(command, wire);
        case DriveKind::kCount:
            return false;
    }
    return false;
}

bool IdeEngine::run_disk_(IdeCommand command, IIdeTransport& wire) noexcept {
    switch (command) {
        case IdeCommand::Identify:
            identify_(wire);
            return true;
        case IdeCommand::ReadMultiple:
            begin_read_(Bursts::Multiple, wire);
            return true;
        case IdeCommand::ReadRetry:
        case IdeCommand::Read:
            begin_read_(Bursts::Single, wire);
            return true;
        case IdeCommand::WriteMultiple:
            begin_write_(Bursts::Multiple, Sink::Image, wire);
            return true;
        case IdeCommand::WriteRetry:
        case IdeCommand::Write:
            begin_write_(Bursts::Single, Sink::Image, wire);
            return true;
        case IdeCommand::SetMultiple:
            if (regs_.count.blocks() > kMostBurst)
                return false;
            selected_disk_().burst = regs_.count.blocks();
            complete_(kDone, wire);
            return true;
        case IdeCommand::Recalibrate:
            regs_.address = regs_.address.recalibrated();
            complete_(kDone, wire);
            return true;
        case IdeCommand::ReadVerify:
        case IdeCommand::Seek:
        case IdeCommand::StandbyImmediate:
            complete_(kDoneSettled, wire);
            return true;
        case IdeCommand::Diagnostic:
            regs_.fault = Fault::DiagnosticPassed;
            complete_(kDoneSettled, wire);
            return true;
        case IdeCommand::InitParameters: {
            DiskDrive& disk = selected_disk_();
            const svc::DiskGeometry named{{}, regs_.address.head_count(), regs_.count.sectors()};
            disk.geometry = named.addressing(disk.image->size());
            complete_(kDone, wire);
            return true;
        }
        case IdeCommand::Nop:
        case IdeCommand::Reset:
        case IdeCommand::Packet:
        case IdeCommand::IdentifyPacket:
        case IdeCommand::SetFeatures:
            return false;
        case IdeCommand::MountImage:
            break;
    }
    return false;
}

bool IdeEngine::run_packet_(IdeCommand command, IIdeTransport& wire) noexcept {
    PacketDrive& packet = selected_packet_();
    switch (command) {
        case IdeCommand::IdentifyPacket: {

            const IdeDriveSelect drive = regs_.drive;
            regs_ = IdeTaskFile{};
            regs_.drive = drive;
            regs_.count = InterruptReason::kDataOut.count();
            regs_.packet = PacketWords::of(kAtapiIdentifyBytes);
            regs_.status = kPacketIdentified;
            wire.send_data(packet.identify);
            write_regs_(wire);
            phase_ = Phase::Idle;
            return true;
        }
        case IdeCommand::Packet: {
            if (regs_.dma_feature())
                return false;
            packet.drive.begin_packet(regs_.address.byte_count());
            regs_.count = InterruptReason::kCommand.count();
            regs_.packet = PacketWords::of(AtapiPacket::kBytes);
            regs_.status = kBurstWanted;
            phase_ = Phase::PacketCommand;
            write_regs_(wire);
            return true;
        }
        case IdeCommand::SetFeatures:
            if (regs_.features != IdeTaskFile::kTransferMode)
                return false;
            regs_.status = kDone;
            write_regs_(wire);
            phase_ = Phase::Idle;
            return true;
        case IdeCommand::Reset: {
            packet.drive.bus_reset();
            regs_.address = IdeAddress::packet_signature();
            regs_.count = InterruptReason::kCommand.count();
            regs_.fault = Fault::DiagnosticPassed;
            regs_.status = IdeStatus{};
            write_regs_(wire);
            phase_ = Phase::Idle;
            return true;
        }
        case IdeCommand::Identify:

            regs_.address = IdeAddress::packet_signature();
            regs_.count = InterruptReason::kCommand.count();
            refusals_.add();
            regs_.fault = Fault::Aborted;
            complete_(kAborted, wire);
            phase_ = Phase::Idle;
            return true;
        case IdeCommand::Nop:
        case IdeCommand::Recalibrate:
        case IdeCommand::ReadRetry:
        case IdeCommand::Read:
        case IdeCommand::WriteRetry:
        case IdeCommand::Write:
        case IdeCommand::ReadVerify:
        case IdeCommand::Seek:
        case IdeCommand::Diagnostic:
        case IdeCommand::InitParameters:
        case IdeCommand::ReadMultiple:
        case IdeCommand::WriteMultiple:
        case IdeCommand::SetMultiple:
        case IdeCommand::StandbyImmediate:
        case IdeCommand::MountImage:
            return false;
    }
    return false;
}

void IdeEngine::packet_step_(const AtapiStep& step, IIdeTransport& wire) noexcept {
    PacketDrive& packet = selected_packet_();
    regs_.packet = PacketWords{};
    if (const auto* in = std::get_if<AtapiStep::DataIn>(&step.phase)) {
        regs_.count = InterruptReason::kDataIn.count();
        regs_.address = regs_.address.with_byte_count(in->bytes);
        regs_.packet = PacketWords::of(in->bytes);
        regs_.fault = Fault::None;
        regs_.packet_sense.reset();
        regs_.status = kPacketBurst;
        phase_ = Phase::PacketDataIn;
        wire.send_data(packet.drive.reply_words());
        write_regs_(wire);
        return;
    }
    if (const auto* out = std::get_if<AtapiStep::DataOut>(&step.phase)) {
        packet_out_ = out->bytes;
        regs_.count = InterruptReason::kDataOut.count();
        regs_.address = regs_.address.with_byte_count(out->bytes);
        regs_.packet = PacketWords::of(out->bytes);
        regs_.fault = Fault::None;
        regs_.packet_sense.reset();
        regs_.status = kPacketBurst;
        phase_ = Phase::PacketDataOut;
        write_regs_(wire);
        return;
    }
    if (const auto* status = std::get_if<AtapiStep::Status>(&step.phase)) {
        regs_.count = InterruptReason::kStatus.count();
        regs_.packet_sense = status->key;
        IdeStatus bits = IdeStatus::kReady | IdeStatus::kIrq;
        if (status->check())
            bits = bits | IdeStatus::kError;
        if (status->seek_complete)
            bits = bits | IdeStatus::kSeekComplete;
        regs_.status = bits;
        phase_ = Phase::Idle;
        transfer_ = Transfer{};
        write_regs_(wire);
        return;
    }

    fatal(Error{Errc::contract, ERR_SITE(), 0}, "IdeEngine: an unknown packet step");
}

void IdeEngine::identify_(IIdeTransport& wire) noexcept {
    const IdeDriveSelect drive = regs_.drive;
    regs_ = IdeTaskFile{};
    regs_.drive = drive;
    regs_.burst = kOneSector;
    regs_.status = kIdentified;
    wire.send_data(selected_disk_().identify.bytes());
    write_regs_(wire);
}

void IdeEngine::begin_read_(Bursts bursts, IIdeTransport& wire) noexcept {
    transfer_ = Transfer{.next = block_(), .bursts = bursts};
    send_burst_(wire);
}

void IdeEngine::send_burst_(IIdeTransport& wire) noexcept {
    transfer_.burst = next_burst_();
    const std::span<std::byte> burst = burst_bytes_();
    if (!transfer_.lost) {
        const auto got = selected_disk_().image->read_at(
            kSector.position_of(transfer_.next),
            burst
        );
        if (!got)
            io_faults_.add();
        if (!got || got->empty())
            transfer_.lost = true;
        else
            got->zero_past_in(burst);
    }
    if (transfer_.lost)
        std::ranges::fill(burst, std::byte{});

    transfer_.next += transfer_.burst;
    regs_.count = regs_.count.less(transfer_.burst);
    park_before_(transfer_.next);
    regs_.burst = transfer_.burst;
    const bool last = regs_.count.none();
    regs_.status = last ? kBurstOut | IdeStatus::kLastRead : kBurstOut;
    if (regs_.fast_read) {
        write_regs_(wire);
        wire.send_data(burst);
    } else {
        wire.send_data(burst);
        regs_.status = regs_.status.without(IdeStatus::kFastRead);
        write_regs_(wire);
    }
    phase_ = last ? Phase::Idle : Phase::Reading;
}

void IdeEngine::begin_write_(Bursts bursts, Sink sink, IIdeTransport& wire) noexcept {
    transfer_ = Transfer{.next = block_(), .bursts = bursts, .sink = sink};
    ask_burst_(IdeStatus{}, wire);
}

void IdeEngine::ask_burst_(IdeStatus irq, IIdeTransport& wire) noexcept {
    transfer_.burst = next_burst_();
    regs_.burst = transfer_.burst;

    if (selected_kind_() == DriveKind::Packet)
        regs_.packet = PacketWords::of(kSector.window_count(transfer_.burst));
    complete_(kBurstWanted | irq, wire);
    phase_ = Phase::WriteReady;
}

void IdeEngine::keep_burst_() noexcept {
    if (!transfer_.lost) {
        const std::span<const std::byte> burst = burst_bytes_();
        const auto put = selected_disk_().image->write_at(
            kSector.position_of(transfer_.next),
            burst
        );
        if (!put || put->empty() || !put->covers(burst)) {
            transfer_.lost = true;
            io_faults_.add();
        }
    }
    transfer_.next += transfer_.burst;
    regs_.count = regs_.count.less(transfer_.burst);
    park_before_(transfer_.next);
}

}
