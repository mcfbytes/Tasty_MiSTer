// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/atari8/pbi_mailbox.h"

#include <algorithm>
#include <utility>

#include "cores/atari8/atari8_wire.h"
#include "cores/atari8/sio_dma.h"
#include "infra/affine.h"
#include "infra/span_cut.h"
#include "proto/types.h"
#include "proto/user_io/poll_frame.h"

namespace mister::cores::atari8 {

namespace {

using wire::Level;
using wire::WriteRegister;

constexpr std::byte kDone{0x01};
constexpr std::byte kFailed{0x90};
constexpr std::byte kNoDevice{0x8A};
constexpr std::byte kNoCommand{0x8B};

constexpr HostAddress kStatusByte{0xDFAD};
constexpr HostAddress kInfoBlock{0xDFA0};

struct Target {
    std::byte device{};
    std::byte unit{};

    static constexpr hal::RegField<std::uint8_t> kFamilyField{0, 0x7F};
    static constexpr hal::RegField<std::uint8_t> kXdcbBit{7, 0x1};
    static constexpr SioDevice kHardDiskFamily{0x20};

    [[nodiscard]] constexpr bool hard_disk() const noexcept {
        return SioDevice{kFamilyField.get(std::to_integer<std::uint8_t>(device))}
        == kHardDiskFamily;
    }

    [[nodiscard]] constexpr std::optional<SioUnit> drive() const noexcept {
        return SioUnit::of_dunit(unit);
    }

    [[nodiscard]] constexpr bool own_hard_disk() const noexcept {
        return hard_disk() && drive() == SioUnit{};
    }

    [[nodiscard]] constexpr SioDevice frame_device() const noexcept {
        return SioDevice::of_pbi_request(SioDevice{std::to_integer<std::uint8_t>(device)}, unit);
    }

    [[nodiscard]] constexpr PbiDriveMode unconfigured_mode() const noexcept {
        return kXdcbBit.test(std::to_integer<std::uint8_t>(device)) ? PbiDriveMode::Pbi
                                                                    : PbiDriveMode::Off;
    }
};

}

void PbiMailbox::begin_core() noexcept {
    TASTY_SEAT_BODY(PbiMailbox);
    step_ = Step::Idle;
    modes_ = {};
}

PbiMailbox::Step PbiMailbox::step() const noexcept {
    TASTY_SEAT_BODY(PbiMailbox);
    return step_;
}

bool PbiMailbox::wants(const SioTerms& t) const noexcept {
    TASTY_SEAT_BODY(PbiMailbox);
    return step_ != Step::Idle || t.status.pbi();
}

infra::ByteCount PbiMailbox::data_length_() const noexcept {
    return std::min(request_.length().bytes(), SioDrives::kBufferBytes);
}

SioCall PbiMailbox::call_(const SioTerms& t) const noexcept {
    return SioCall{
        .bus = SioBus::Pbi,
        .direction = request_.direction(),
        .length = request_.length().bytes(),
        .host = infra::affine::first(std::span<const std::byte>{host_}, host_bytes_),
        .status = t.status,
        .now = t.now
    };
}

proto::MailboxAct PbiMailbox::next(ActBytes bytes, const SioTerms& t) {
    TASTY_SEAT_BODY(PbiMailbox);
    if (step_ == Step::Idle) {
        if (!t.status.pbi())
            return {};
        step_ = Step::Poll;
    }
    proto::MailboxRun run{bytes};
    switch (step_) {
        case Step::Idle:
            return {};
        case Step::Poll:
            return dma_read(host_at(PbiMailboxBlock::kBase), PbiMailboxBlock::kBytes);
        case Step::Options:
            (void)run.add(wire::get(wire::ReadRegister::Status2));
            return wire::run_act(run);
        case Step::InitConfig: {
            const HostRun config = mailbox_.config_run();
            return dma_write(bytes, host_at(config.at), config.bytes);
        }
        case Step::InitFlag: {
            const HostRun flag = mailbox_.flag_run();
            return dma_write(bytes, host_at(flag.at), flag.bytes);
        }
        case Step::LedOn:
            (void)run.add(wire::set(WriteRegister::DriveLed, Level::High));
            return wire::run_act(run);
        case Step::Request:
            return dma_read(host_at(HostDcb::kBase), HostDcb::kBytes);
        case Step::Data:
            return dma_read(
                host_at(request_.buffer().after(moved_)),
                std::min(kMostUpload, data_length_() - moved_)
            );
        case Step::Prefetch: {
            const bool status = verb_ == SioCommand::Status;
            const HostAddress from = status ? kStatusByte : kInfoBlock;
            const infra::ByteCount want = status ? infra::ByteCount{1} : kHostPrefetch;
            return dma_read(
                host_at(from.after(host_bytes_)),
                std::min(kMostUpload, want - host_bytes_)
            );
        }
        case Step::Reply:
            return dma_write(
                bytes,
                host_at(request_.buffer()),
                reply_.bytes.first_in(std::span<const std::byte>{buffer_})
            );
        case Step::Length: {
            const HostRun length = request_.length_run();
            return dma_write(bytes, host_at(length.at), length.bytes);
        }
        case Step::Result: {
            const HostRun status = request_.status_run();
            return dma_write(bytes, host_at(status.at), status.bytes);
        }
        case Step::LedOff:
            (void)run.add(wire::set(WriteRegister::DriveLed, Level::Low));
            return wire::run_act(run);
        case Step::Answer: {
            const HostRun answer = mailbox_.answer_run();
            return dma_write(bytes, host_at(answer.at), answer.bytes);
        }
    }
    return {};
}

void PbiMailbox::settled(const proto::MailboxResult& result, ActBytes bytes, const SioTerms& t) {
    TASTY_SEAT_BODY(PbiMailbox);

    if (result.end != proto::MailboxResult::End::Done)
        return;
    const auto read = [bytes](std::span<std::byte> into) {
        std::ranges::copy(std::as_bytes(std::span{bytes}).first(into.size()), into.begin());
    };
    switch (step_) {
        case Step::Idle:
            return;
        case Step::Poll:
            read(mailbox_.bytes());
            step_ = mailbox_.marked() ? Step::Options : Step::Idle;
            return;
        case Step::Options: {
            const proto::user_io::PollFrame& r = result.replies;
            if (r.kept == proto::ReplyCount{})
                return;
            options_ = Atari8Status2{r.replies.prefix(r.kept).back()};
            if (mailbox_.init_asked()) {
                modes_ = options_;
                const Atari8Status2::BootDrive boot = options_.boot_drive();
                std::byte boot_byte = PbiMailboxBlock::kCleared;
                const infra::OptRef<const SioDrive> hdd = std::as_const(t.drives).drive(
                    SioDrives::kHardDisk
                );
                if (boot.apt() && hdd && hdd->medium() == SioDrive::Medium::Disk) {
                    boot_byte = hdd->boot_partition().wire_byte();
                } else if (boot.chosen()) {
                    boot_byte = boot.drive_byte();
                }
                mailbox_.configure(modes_, boot_byte);
                step_ = Step::InitConfig;
            } else if (mailbox_.request_asked()) {
                step_ = Step::LedOn;
            } else {
                step_ = Step::Idle;
            }
            return;
        }
        case Step::InitConfig:
            mailbox_.take_init(options_.splash());
            step_ = Step::InitFlag;
            return;
        case Step::InitFlag:
        case Step::Answer:
            step_ = Step::Idle;
            return;
        case Step::LedOn:
            step_ = Step::Request;
            return;
        case Step::Request:
            read(request_.bytes());
            request_read_(t);
            return;
        case Step::Data: {
            const infra::ByteCount n = std::min(kMostUpload, data_length_() - moved_);
            read(infra::affine::first(infra::affine::from(std::span{buffer_}, moved_), n));
            moved_ += n;
            if (moved_ < data_length_())
                return;
            begin_verb_(t);
            return;
        }
        case Step::Prefetch: {
            const infra::ByteCount want = verb_ == SioCommand::Status ? infra::ByteCount{1}
                                                                      : kHostPrefetch;
            const infra::ByteCount n = std::min(kMostUpload, want - host_bytes_);
            read(infra::affine::first(infra::affine::from(std::span{host_}, host_bytes_), n));
            host_bytes_ += n;
            if (host_bytes_ < want)
                return;
            run_verb_(t);
            return;
        }
        case Step::Reply:
            step_ = Step::Length;
            return;
        case Step::Length:
            step_ = Step::Result;
            return;
        case Step::Result:
            step_ = Step::LedOff;
            return;
        case Step::LedOff:
            step_ = Step::Answer;
            return;
    }
}

void PbiMailbox::request_read_(const SioTerms& t) {
    const Target target{.device = request_.device(), .unit = request_.unit()};
    frame_ = SioFrame{.device = target.frame_device(), .code = request_.command()};

    frame_.sector = request_.sector(frame_.device.long_sector());

    const std::optional<SioUnit> drive = target.drive();
    const bool own_hard_disk = target.own_hard_disk();
    PbiDriveMode mode = target.unconfigured_mode();
    if (own_hard_disk) {
        mode = PbiDriveMode::Pbi;
    } else if (!target.hard_disk() && drive) {
        mode = modes_.mode_of(*drive).value_or(mode);
    }
    unit_ = own_hard_disk ? SioDrives::kHardDisk : drive.value_or(SioUnit{});
    const infra::OptRef<const SioDrive> d = drive || own_hard_disk
        ? std::as_const(t.drives).drive(unit_)
        : infra::OptRef<const SioDrive>{};
    const bool opened = d && d->medium() != SioDrive::Medium::Empty;
    if (opened) {
        switch (d->medium()) {
            case SioDrive::Medium::Partition:
            case SioDrive::Medium::Disk:
                if (!target.hard_disk())
                    mode = PbiDriveMode::Pbi;
                break;
            case SioDrive::Medium::Timed:
                mode = PbiDriveMode::Off;
                break;
            case SioDrive::Medium::Boot:
                mode = PbiDriveMode::Pbi;
                break;
            case SioDrive::Medium::Flat:
            case SioDrive::Medium::Empty:
                break;
        }
    }

    if (opened && mode == PbiDriveMode::Sio && d->sector_size() == kCardSector)
        mode = PbiDriveMode::Off;
    if (mode == PbiDriveMode::Off) {

        mailbox_.answer(mode);
        step_ = Step::Answer;
        return;
    }
    if (!opened || mode == PbiDriveMode::Sio) {
        if (mode == PbiDriveMode::Pbi)
            request_.set_status(kNoDevice);
        finish_(mode);
        return;
    }
    verb_ = t.drives.verb_for(frame_, call_(t));
    if (!verb_) {
        request_.set_status(kNoCommand);
        finish_(mode);
        return;
    }
    std::ranges::fill(buffer_, std::byte{});
    host_bytes_ = infra::ByteCount{};
    moved_ = infra::ByteCount{};
    if (data_length_() != infra::ByteCount{}) {
        step_ = Step::Data;
        return;
    }
    begin_verb_(t);
}

void PbiMailbox::begin_verb_(const SioTerms& t) {
    const bool reads_host = unit_ == SioDrives::kHardDisk
        && (verb_ == SioCommand::Status || verb_ == SioCommand::DeviceInfo);
    if (reads_host) {
        host_bytes_ = infra::ByteCount{};
        step_ = Step::Prefetch;
        return;
    }
    run_verb_(t);
}

void PbiMailbox::run_verb_(const SioTerms& t) {
    reply_ = t.drives.execute(*verb_, frame_, unit_, call_(t), buffer_);
    reply_.bytes = std::min(reply_.bytes, infra::ByteCount{buffer_.size()});
    request_.set_status(reply_.success ? kDone : kFailed);
    finish_(PbiDriveMode::Pbi);
    if (!reply_.respond)
        return;
    request_.set_length(DcbLength::of(reply_.bytes));
    step_ = reply_.bytes != infra::ByteCount{} ? Step::Reply : Step::Length;
}

void PbiMailbox::finish_(PbiDriveMode mode) noexcept {
    mailbox_.answer(mode);
    step_ = Step::Result;
}

}
