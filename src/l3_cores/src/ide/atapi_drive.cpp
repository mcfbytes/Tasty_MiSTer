// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/ide/atapi_drive.h"

#include <algorithm>

#include "cores/ide/atapi_packet_op.h"
#include "cores/ide/atapi_replies.h"
#include "cores/ide/atapi_toc.h"
#include "cores/ide/ide_task_file.h"
#include "infra/byte_offset.h"
#include "infra/indexed_span.h"
#include "infra/span_cut.h"
#include "svc/disc/disc_ask.h"

namespace mister::cores::ide {

namespace {

using proto::BlockCount;
using proto::Lba;
using svc::disc::DiscForm;

constexpr svc::disc::Toc kEmptyTable{};

constexpr infra::ByteOffset kSelectPageAt{8};
constexpr infra::ByteOffset kSelectPort0At = kSelectPageAt + infra::ByteCount{9};
constexpr infra::ByteOffset kSelectPort1At = kSelectPageAt + infra::ByteCount{11};
constexpr std::byte kSelectPageMask{0x3F};
constexpr std::byte kSelectAudioPage = wire_byte(AtapiModePage::AudioControl);

constexpr svc::TrackIndex kInvisibleRow{0xFF};

}

constexpr AtapiDrive::Load AtapiDrive::settled_(Load load) noexcept {
    switch (load) {
        case Load::SpinningUp:
            return Load::Settling;
        case Load::Settling:
            return Load::Ready;
        case Load::Ready:
        case Load::NoDisc:
            break;
    }
    return load;
}

void AtapiDrive::insert(Disc disc) noexcept {
    TASTY_SEAT_BODY(AtapiDrive);
    disc_.emplace(disc);
    load_ = Load::Settling;
    swapped_ = true;
    play_ = Play{};
}

void AtapiDrive::remove() noexcept {
    TASTY_SEAT_BODY(AtapiDrive);
    disc_.reset();
    load_ = Load::NoDisc;
    swapped_ = true;
    play_ = Play{};
}

void AtapiDrive::power_on() noexcept {
    TASTY_SEAT_BODY(AtapiDrive);
    volumes_ = Volumes{};
    swapped_ = false;
}

void AtapiDrive::bus_reset() noexcept {
    TASTY_SEAT_BODY(AtapiDrive);
    play_.state = PlayState::Stopped;
}

void AtapiDrive::begin_packet(infra::ByteCount limit) noexcept {
    TASTY_SEAT_BODY(AtapiDrive);
    limit_ = limit == infra::ByteCount{} ? kBuffer : limit;
}

std::span<const std::byte> AtapiDrive::reply() const noexcept {
    return infra::affine::first_clamped(std::span<const std::byte>{buf_}, reply_len_);
}

std::span<const std::byte> AtapiDrive::reply_words() const noexcept {
    return infra::affine::first_clamped(
        std::span<const std::byte>{buf_},
        PacketWords::of(reply_len_).bytes()
    );
}

const svc::disc::Toc& AtapiDrive::table_() const noexcept {
    return disc_ ? disc_->toc : kEmptyTable;
}

AtapiStep AtapiDrive::complete_(
    AtapiSense sense,
    Attention attention,
    bool seek_complete
) noexcept {
    if (swapped_ && attention == Attention::Report) {
        swapped_ = false;
        sense_ = AtapiSense{
            .key = AtapiSenseKey::UnitAttention,
            .asc = AtapiAsc::MediumMayHaveChanged
        };
        return AtapiStep{AtapiStep::Status{.key = AtapiSenseKey::UnitAttention}};
    }
    sense_ = sense;
    return AtapiStep{AtapiStep::Status{.key = sense.key, .seek_complete = seek_complete}};
}

AtapiStep AtapiDrive::refuse_(AtapiAsc why) noexcept {
    return complete_(AtapiSense{.key = AtapiSenseKey::IllegalRequest, .asc = why});
}

AtapiStep AtapiDrive::not_ready_() noexcept {
    load_ = settled_(load_);
    return complete_(AtapiSense{.key = AtapiSenseKey::NotReady});
}

AtapiStep AtapiDrive::send_(infra::ByteCount bytes) noexcept {
    reply_len_ = bytes;
    return AtapiStep{AtapiStep::DataIn{bytes}};
}

AtapiStep AtapiDrive::packet(const AtapiPacket& cmd) noexcept {
    TASTY_SEAT_BODY(AtapiDrive);
    read_ = {};
    const std::optional<AtapiPacketOp> op = cmd.op();
    const bool ready = load_ == Load::Ready && !swapped_;
    if (!ready && !(op && answers_when_not_ready(*op)))
        return not_ready_();
    if (!op)
        return refuse_(AtapiAsc::IllegalOpcode);

    const AtapiToc toc{table_()};
    const std::span<std::byte> out{buf_};
    switch (*op) {
        case AtapiPacketOp::Read10:
            return read_blocks_({.start = cmd.lba(), .len = cmd.blocks10()});
        case AtapiPacketOp::Read12:
            return read_blocks_({.start = cmd.lba(), .len = cmd.blocks12()});
        case AtapiPacketOp::ReadCapacity:
            return send_(write_capacity(toc, out));
        case AtapiPacketOp::Seek10:
            play_.state = PlayState::Stopped;
            return complete_(AtapiSense{}, Attention::Report, true);
        case AtapiPacketOp::TestUnitReady:
        case AtapiPacketOp::StartStopUnit:
        case AtapiPacketOp::PreventAllowRemoval:
        case AtapiPacketOp::SynchronizeCache:
            return good_();
        case AtapiPacketOp::ModeSense10:
            return send_(write_mode_pages(cmd.page(), volumes_.port0, out));
        case AtapiPacketOp::ReadSubchannel:
            return answer_subchannel_(cmd);
        case AtapiPacketOp::ReadToc:
            return send_(write_toc(toc, cmd, out));
        case AtapiPacketOp::StopPlayScan:
            play_.state = PlayState::Stopped;
            return good_();
        case AtapiPacketOp::Inquiry:
            return send_(write_inquiry(cmd.short_alloc(), out));
        case AtapiPacketOp::GetConfiguration:
            if (!cmd.from_first_feature())
                return refuse_(AtapiAsc::IllegalField);
            if (cmd.alloc() == infra::ByteCount{})
                return good_();
            return send_(write_configuration(cmd.alloc(), out));
        case AtapiPacketOp::ReadDiscInformation:
            if (!cmd.standard_disc_info())
                return refuse_(AtapiAsc::IllegalOpcode);
            return send_(write_disc_information(toc, cmd.alloc(), out));
        case AtapiPacketOp::ReadTrackInformation:
            return answer_track_information_(cmd);
        case AtapiPacketOp::RequestSense:
            return send_(answer_sense_());
        case AtapiPacketOp::ModeSelect10:
            return AtapiStep{AtapiStep::DataOut{std::min(cmd.alloc(), kParameterBytes)}};
        case AtapiPacketOp::PlayAudio10:
            start_play_({.from = cmd.lba(), .to = cmd.lba() + cmd.blocks10()});
            return good_();
        case AtapiPacketOp::PlayAudioMsf: {
            const AtapiPacket::MsfAddress from = cmd.play_from();
            const AtapiPacket::MsfAddress to = cmd.play_to();
            start_play_(
                {.from = from.current() ? kCurrentPosition : from.lba(),
                 .to = to.current() ? kCurrentPosition : to.lba()}
            );
            return good_();
        }
        case AtapiPacketOp::PauseResume:
            return pause_resume_(cmd);
        case AtapiPacketOp::GetEventStatus:
            break;
    }
    return refuse_(AtapiAsc::IllegalOpcode);
}

AtapiStep AtapiDrive::data_in_done() noexcept {
    TASTY_SEAT_BODY(AtapiDrive);
    if (read_.len != BlockCount{})
        return serve_read_();
    return complete_(AtapiSense{}, Attention::Hold);
}

AtapiStep AtapiDrive::data_out(std::span<const std::byte> parameters) noexcept {
    TASTY_SEAT_BODY(AtapiDrive);

    const infra::IndexedSpan<const std::byte, infra::ByteOffset> list{parameters};
    const auto port1 = list.find(kSelectPort1At);
    if (port1 && (list[kSelectPageAt] & kSelectPageMask) == kSelectAudioPage) {
        volumes_ = Volumes{
            .port0 = AtapiVolume::of(list[kSelectPort0At]),
            .port1 = AtapiVolume::of(*port1)
        };
    }
    return good_();
}

AtapiStep AtapiDrive::read_blocks_(infra::affine::Run<Lba> run) noexcept {
    read_ = run;
    if (run.len == BlockCount{})
        return good_();
    return serve_read_();
}

AtapiStep AtapiDrive::serve_read_() noexcept {
    BlockCount burst = std::min(read_.len, kBurstBlocks);
    while (burst > BlockCount{1} && infra::ByteCount{kBlock.window_bytes(burst)} > limit_)
        burst -= BlockCount{1};

    infra::ByteOffset at{};
    for (BlockCount done{}; done < burst; done += BlockCount{1}) {
        fill_block_(
            read_.start + done,
            infra::affine::first(infra::affine::from(std::span<std::byte>{buf_}, at), kBlockBytes)
        );
        at += kBlockBytes;
    }
    read_.start += burst;
    read_.len -= burst;
    return send_(infra::ByteCount{kBlock.window_bytes(burst)});
}

void AtapiDrive::fill_block_(Lba block, std::span<std::byte> user_data) noexcept {
    if (disc_) {
        const auto got = disc_->reader.read_form(DiscForm::UserData, block, user_data);
        if (got && got->size() == user_data.size())
            return;
    }
    std::ranges::fill(user_data, std::byte{0});
}

infra::ByteCount AtapiDrive::answer_sense_() noexcept {
    AtapiSense now{};
    switch (load_) {
        case Load::NoDisc:
            now = AtapiSense{.key = AtapiSenseKey::NotReady, .asc = AtapiAsc::MediumNotPresent};
            break;
        case Load::SpinningUp:
            now = AtapiSense{
                .key = AtapiSenseKey::NotReady,
                .asc = AtapiAsc::BecomingReady,
                .ascq = AtapiAscq::InProgress
            };
            load_ = settled_(load_);
            break;
        case Load::Settling:
            now = AtapiSense{.key = AtapiSenseKey::NotReady, .asc = AtapiAsc::LoadSettled};
            load_ = settled_(load_);
            break;
        case Load::Ready:
            now = sense_;
            if (sense_.key == AtapiSenseKey::UnitAttention)
                sense_ = AtapiSense{};
            break;
    }
    return now.store(std::span<std::byte>{buf_});
}

AtapiStep AtapiDrive::answer_track_information_(const AtapiPacket& cmd) noexcept {
    const AtapiToc toc{table_()};
    const svc::TrackIndex row = cmd.info_row();
    if (!cmd.info_by_track() || row > toc.rows().next_index() || row == kInvisibleRow)
        return refuse_(AtapiAsc::IllegalOpcode);
    return send_(write_track_information(toc, row, cmd.alloc(), std::span<std::byte>{buf_}));
}

AtapiStep AtapiDrive::answer_subchannel_(const AtapiPacket& cmd) noexcept {
    AtapiAudioStatus status = AtapiAudioStatus::Stopped;
    switch (play_.state) {
        case PlayState::Playing:
            status = AtapiAudioStatus::Playing;
            break;
        case PlayState::Paused:
            status = AtapiAudioStatus::Paused;
            break;
        case PlayState::Stopped:
        case PlayState::Parked:
            break;
    }
    const std::optional<Lba> head = play_.head == kCurrentPosition ? std::nullopt
                                                                   : std::optional<Lba>{play_.head};
    return send_(
        write_subchannel(AtapiToc{table_()}, cmd, status, head, std::span<std::byte>{buf_})
    );
}

AtapiStep AtapiDrive::pause_resume_(const AtapiPacket& cmd) noexcept {
    const bool on_a_track = play_.state == PlayState::Playing || play_.state == PlayState::Paused;
    if (on_a_track)
        play_.state = cmd.resume() ? PlayState::Playing : PlayState::Paused;
    return complete_(
        AtapiSense{
            .key = on_a_track ? AtapiSenseKey::NoSense : AtapiSenseKey::IllegalRequest,
            .asc = AtapiAsc::CommandSequence
        }
    );
}

void AtapiDrive::start_play_(PlayEnds ends) noexcept {
    play_.head = ends.from;
    play_.end = ends.to;
    if (ends.from == ends.to)
        play_.state = PlayState::Stopped;
    else
        play_.state = ends.from == kCurrentPosition ? PlayState::Parked : PlayState::Playing;
}

bool AtapiDrive::play_frame(std::span<std::byte, svc::disc::kRawFrameBytes> frame) noexcept {
    TASTY_SEAT_BODY(AtapiDrive);
    if (!playing())
        return false;
    bool served = false;
    if (disc_) {
        const AtapiToc toc{disc_->toc};
        const std::optional<AtapiToc::Hit> hit = toc.row_at(play_.head);
        if (hit && toc.row(hit->row).type == svc::disc::TrackType::Cdda) {
            const auto got = disc_->reader.read_form(DiscForm::RawFrame, play_.head, frame);
            served = got && got->size() == frame.size();
        }
    }
    if (!served)
        std::ranges::fill(frame, std::byte{0});
    play_.head += BlockCount{1};
    if (play_.head >= play_.end)
        play_.state = PlayState::Stopped;
    return true;
}

}
