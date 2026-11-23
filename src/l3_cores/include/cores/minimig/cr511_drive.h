// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "cores/mailbox_servant.h"
#include "cores/minimig/cd_disc.h"
#include "cores/minimig/cd_status.h"
#include "cores/minimig/cr511_address.h"
#include "cores/minimig/cr511_wire.h"
#include "cores/nvram_store.h"
#include "hal/reg_field.h"
#include "infra/byte_count.h"
#include "infra/byte_offset.h"
#include "infra/endian.h"
#include "infra/fixed_ring.h"
#include "infra/seat.h"
#include "infra/span_cut.h"
#include "proto/mailbox_act.h"
#include "proto/mailbox_result.h"
#include "proto/types.h"
#include "svc/disc/toc_point.h"
#include "svc/disc/track.h"
#include "svc/sector_types.h"
#include "svc/types.h"

namespace mister::cores::minimig {

struct StchBudget {
    TASTY_SEAT_EXEMPT(component);
    unsigned v = 0;

    static const StchBudget kMediaChange;
    static const StchBudget kPlayEnd;

    [[nodiscard]] static constexpr StchBudget none() noexcept { return StchBudget{}; }

    [[nodiscard]] constexpr bool pending() const noexcept { return v > 0; }

    constexpr void spend() noexcept {
        if (v > 0)
            --v;
    }

    [[nodiscard]] constexpr StchBudget spent() const noexcept {
        return v > 0 ? StchBudget{v - 1} : *this;
    }

    friend constexpr bool operator==(StchBudget, StchBudget) = default;
};

inline constexpr StchBudget StchBudget::kMediaChange{40};
inline constexpr StchBudget StchBudget::kPlayEnd{400};

class Cr511Drive {
    TASTY_SEAT_RESIDENT(Pcm);

public:
    using BulkBytes = IMailboxServant::BulkBytes;
    using Instant = proto::MailboxAct::NotBefore;

    static constexpr std::chrono::milliseconds kSwapSettle{700};

    static constexpr std::chrono::milliseconds kStchPeriod{250};

    static constexpr std::chrono::milliseconds kPlayEndPeriod{60};

    static constexpr std::chrono::milliseconds kSectorGap{1};

    static constexpr infra::ByteCount kSectorPieceBytes{1176};

    enum class Pending : std::uint8_t { None, Write, StchAck };

    void init(Instant now) noexcept;

    void forget(Instant now) noexcept;

    void set_active(bool on) noexcept { active_ = on; }

    void bind(std::optional<CdDisc> disc, std::string_view path, Instant now) noexcept;

    void on_status(CdStatus status) noexcept;

    void on_byte(std::byte byte) noexcept;

    void settled(const proto::MailboxResult& result, std::span<const std::byte> bulk) noexcept;
    [[nodiscard]] proto::MailboxAct next(Instant now, BulkBytes bulk) noexcept;
    [[nodiscard]] std::optional<Instant> deadline(Instant now) const noexcept;

    [[nodiscard]] Pending pending() const noexcept { return pending_; }

    [[nodiscard]] bool media() const noexcept { return media_; }

    [[nodiscard]] StchBudget stch_retries() const noexcept { return stch_retries_; }

    [[nodiscard]] svc::SectorSize sector_size() const noexcept { return sector_size_; }

    [[nodiscard]] std::string_view nvram_path() const noexcept { return nvram_path_.view(); }

    [[nodiscard]] std::string_view card_path() const noexcept { return card_path_.view(); }

private:
    struct DriveFlags {
        std::uint8_t v = 0;

        static const DriveFlags kNotReady;
        static const DriveFlags kPlaying;
        static const DriveFlags kFinished;
        static const DriveFlags kError;
        static const DriveFlags kMotor;
        static const DriveFlags kMedia;

        [[nodiscard]] constexpr std::byte wire_byte() const noexcept { return std::byte{v}; }

        [[nodiscard]] constexpr DriveFlags with(DriveFlags other) const noexcept {
            return DriveFlags{static_cast<std::uint8_t>(v | other.v)};
        }

        friend constexpr bool operator==(DriveFlags, DriveFlags) = default;
    };

    struct CommandFrame {
        TASTY_SEAT_EXEMPT(component);
        std::array<std::byte, cr511::kFrameMax> v{};

        struct PlayLsn {
            proto::Lba from;
            proto::BlockCount count;
        };

        struct PlayTracks {
            svc::TrackNumber from;
            svc::TrackNumber to;
        };

        [[nodiscard]] constexpr cr511::Op op() const noexcept {
            return *cr511::op_of(field<1>(kOpAt).front());
        }

        [[nodiscard]] constexpr proto::Lba read_start() const noexcept {
            return cr511::Address::load(field<3>(kAddressAt)).lba();
        }

        [[nodiscard]] constexpr proto::BlockCount read_count() const noexcept {
            return proto::BlockCount{infra::load_be<std::uint16_t>(field<2>(kCountAt))};
        }

        [[nodiscard]] constexpr PlayLsn play_lsn() const noexcept {
            return PlayLsn{
                .from = cr511::Address::load(field<3>(kAddressAt)).lba(),
                .count = cr511::Address::load(field<3>(kCountAt)).count()
            };
        }

        [[nodiscard]] constexpr cr511::Address::Pair play_msf() const noexcept {
            return cr511::Address::Pair::load(field<6>(kAddressAt));
        }

        [[nodiscard]] constexpr PlayTracks play_tracks() const noexcept {
            return PlayTracks{
                .from = svc::TrackNumber::truncating(
                    std::to_integer<unsigned>(field<1>(kTrackFromAt).front())
                ),
                .to = svc::TrackNumber::truncating(
                    std::to_integer<unsigned>(field<1>(kTrackToAt).front())
                )
            };
        }

        [[nodiscard]] constexpr cr511::AddressForm address_form() const noexcept {
            return kMsfFlag.test(std::to_integer<std::uint8_t>(field<1>(kFlagsAt).front()))
                ? cr511::AddressForm::Msf
                : cr511::AddressForm::Lba;
        }

        [[nodiscard]] constexpr std::optional<svc::disc::TocPoint> toc_descriptor() const noexcept {
            const std::byte point = toc_byte();
            if (point == svc::disc::TocPoint::first_track_info().wire_byte())
                return svc::disc::TocPoint::first_track_info();
            if (point == svc::disc::TocPoint::last_track_info().wire_byte())
                return svc::disc::TocPoint::last_track_info();
            if (point == svc::disc::TocPoint::lead_out().wire_byte())
                return svc::disc::TocPoint::lead_out();
            return std::nullopt;
        }

        [[nodiscard]] constexpr std::optional<svc::TrackNumber> toc_track() const noexcept {
            const svc::TrackNumber number = svc::TrackNumber::truncating(
                std::to_integer<unsigned>(toc_byte())
            );
            if (number == svc::TrackNumber{} || number > svc::kLastTrackNumber)
                return std::nullopt;
            return number;
        }

        [[nodiscard]] constexpr std::byte toc_byte() const noexcept {
            return field<1>(kTocAt).front();
        }

        [[nodiscard]] constexpr svc::SectorSize mode_size() const noexcept {
            return svc::SectorSize{infra::load_be<std::uint16_t>(field<2>(kModeSizeAt))};
        }

        [[nodiscard]] constexpr bool pause_requested() const noexcept {
            return field<1>(kPauseAt).front() == std::byte{};
        }

        constexpr void store(infra::ByteCount at, std::byte byte) noexcept {
            infra::affine::from(std::span{v}, at).front() = byte;
        }

    private:
        static constexpr infra::ByteOffset kOpAt{0};
        static constexpr infra::ByteOffset kAddressAt{1};
        static constexpr infra::ByteOffset kCountAt{4};
        static constexpr infra::ByteOffset kFlagsAt{1};
        static constexpr infra::ByteOffset kTrackFromAt{1};
        static constexpr infra::ByteOffset kTrackToAt{3};
        static constexpr infra::ByteOffset kTocAt{2};
        static constexpr infra::ByteOffset kModeSizeAt{2};
        static constexpr infra::ByteOffset kPauseAt{1};
        static constexpr hal::RegField<std::uint8_t> kMsfFlag{0, 0x02};

        template <std::size_t N>
        [[nodiscard]] constexpr std::span<const std::byte, N> field(
            infra::ByteOffset at
        ) const noexcept {
            return infra::affine::from(std::span{v}, at).template first<N>();
        }
    };

    struct Framer {
        CommandFrame partial{};
        infra::ByteCount have{};
        infra::ByteCount need{};

        [[nodiscard]] std::optional<CommandFrame> push(std::byte byte) noexcept;

    private:
        static constexpr infra::ByteCount kFrameCap{cr511::kFrameMax};
    };

    struct ReplyFrame {
        std::array<std::byte, cr511::kFrameMax> data{};
        infra::ByteCount len{};

        [[nodiscard]] static ReplyFrame none() noexcept;
        [[nodiscard]] static ReplyFrame sync() noexcept;
        [[nodiscard]] static ReplyFrame status(DriveFlags flags) noexcept;
        [[nodiscard]] static ReplyFrame error_info(bool error) noexcept;
        [[nodiscard]] static ReplyFrame model() noexcept;
        [[nodiscard]] static ReplyFrame mode(svc::SectorSize size) noexcept;
        [[nodiscard]] static ReplyFrame capacity(cr511::Address end, svc::SectorSize size) noexcept;
        [[nodiscard]] static ReplyFrame subq(
            cr511::AudioStatus audio,
            svc::TrackNumber track,
            cr511::Address::Pair at
        ) noexcept;
        [[nodiscard]] static ReplyFrame toc(
            std::byte point,
            svc::TrackCount real,
            cr511::Address at
        ) noexcept;
        [[nodiscard]] static ReplyFrame disc_info(
            svc::TrackCount real,
            cr511::Address end
        ) noexcept;
        [[nodiscard]] static ReplyFrame play(bool accepted) noexcept;
        [[nodiscard]] static ReplyFrame blank(infra::ByteCount n) noexcept;

        [[nodiscard]] bool empty() const noexcept { return len.empty(); }

        [[nodiscard]] std::span<const std::byte> bytes() const& noexcept {
            return len.first_in(std::span<const std::byte>{data});
        }

        std::span<const std::byte> bytes() const&& = delete;

    private:
        static constexpr std::byte kSyncFirst{0xAA};
        static constexpr std::byte kSyncSecond{0x55};
        static constexpr std::byte kPlayAccepted{0x42};
        static constexpr std::byte kPlayRefused{0x00};
        static constexpr std::byte kIndex01{proto::PackedBcd::unmasked_byte(1)};

        static constexpr infra::ByteCount kSyncLen{2};
        static constexpr infra::ByteCount kStatusLen{1};
        static constexpr infra::ByteCount kErrorLen{6};
        static constexpr infra::ByteCount kModeLen{2};
        static constexpr infra::ByteCount kCapacityLen{5};
        static constexpr infra::ByteCount kSubqLen{13};
        static constexpr infra::ByteCount kTocLen{8};
        static constexpr infra::ByteCount kInfoLen{5};
        static constexpr infra::ByteCount kPlayLen{1};

        static constexpr infra::ByteOffset kFirstAt{0};
        static constexpr infra::ByteOffset kSecondAt{1};
        static constexpr infra::ByteOffset kFlagAt{2};
        static constexpr infra::ByteOffset kPayloadAt{1};
        static constexpr infra::ByteOffset kTrackAt{2};
        static constexpr infra::ByteOffset kIndexAt{3};
        static constexpr infra::ByteOffset kAddressAt{5};
        static constexpr infra::ByteOffset kRelativeAt{9};
        static constexpr infra::ByteOffset kCapacitySizeAt{3};
        static constexpr infra::ByteOffset kInfoCountAt{1};
        static constexpr infra::ByteOffset kInfoEndAt{2};

        constexpr void put(infra::ByteOffset at, std::byte byte) noexcept {
            infra::affine::from(std::span{data}, at).front() = byte;
        }

        constexpr void put_count(infra::ByteOffset at, svc::TrackCount count) noexcept {
            count.store_byte(infra::affine::from(std::span{data}, at).first<1>());
        }

        constexpr void put_track(infra::ByteOffset at, svc::TrackNumber track) noexcept {
            track.store_byte(infra::affine::from(std::span{data}, at).first<1>());
        }

        constexpr void put_address(infra::ByteOffset at, cr511::Address address) noexcept {
            address.store(infra::affine::from(std::span{data}, at).first<3>());
        }

        constexpr void put_size(infra::ByteOffset at, svc::SectorSize size) noexcept {
            size.store_be16(infra::affine::from(std::span{data}, at).first<2>());
        }
    };

    struct Read {
        proto::Lba lba{};
        proto::BlockCount left{};
        std::array<std::byte, svc::disc::kRawFrameBytes> slice{};
        infra::ByteCount staged{};
        infra::ByteCount sent{};
        std::optional<Instant> after{};
    };

    struct Play {
        proto::Lba next{};
        proto::Lba end{};
    };

    static constexpr std::size_t kQueued = 4;

    [[nodiscard]] proto::MailboxAct stch_(Instant now, BulkBytes bulk) noexcept;
    [[nodiscard]] proto::MailboxAct cdda_(Instant now, BulkBytes bulk) noexcept;
    [[nodiscard]] proto::MailboxAct read_data_(Instant now, BulkBytes bulk) noexcept;
    [[nodiscard]] proto::MailboxAct command_(BulkBytes bulk) noexcept;

    [[nodiscard]] ReplyFrame dispatch_(const CommandFrame& c) noexcept;
    [[nodiscard]] ReplyFrame cmd_play_(const CommandFrame& c) noexcept;
    [[nodiscard]] ReplyFrame cmd_subq_(const CommandFrame& c) const noexcept;
    [[nodiscard]] ReplyFrame cmd_read_toc_(const CommandFrame& c) noexcept;
    [[nodiscard]] ReplyFrame cmd_info_() noexcept;
    void stop_play_() noexcept;
    void play_end_(cr511::AudioStatus audio_status) noexcept;

    [[nodiscard]] bool stage_sector_(Read& r) noexcept;

    [[nodiscard]] std::span<const Track> tracks_() const noexcept;

    [[nodiscard]] bool has_tracks_() const noexcept { return !tracks_().empty(); }

    [[nodiscard]] svc::TrackCount real_tracks_() const noexcept;
    [[nodiscard]] proto::Lba disc_end_() const noexcept;

    [[nodiscard]] proto::MailboxAct written_(proto::MailboxAct act) noexcept;
    [[nodiscard]] proto::MailboxAct inject_stch_(BulkBytes bulk) noexcept;

    std::optional<CdDisc> disc_;
    CdStatus status_{};
    bool active_ = false;

    bool motor_ = false;
    bool isready_ = false;
    bool media_ = false;
    bool playing_ = false;
    bool finished_ = false;
    bool error_ = false;
    bool paused_ = false;
    svc::SectorSize sector_size_ = cr511::kUserData;
    cr511::AudioStatus audio_status_ = cr511::AudioStatus::NoStatus;
    std::optional<Play> play_{};
    proto::Lba last_lba_{};
    std::string path_{};

    StchBudget stch_retries_{};
    std::optional<Instant> stch_next_{};
    bool stch_wait_ack_ = false;
    bool stch_playstart_ = false;
    bool swap_pending_ = false;
    std::optional<Instant> swap_ready_{};

    Framer framer_{};
    infra::FixedRing<CommandFrame, kQueued> commands_{};
    std::optional<Read> read_{};
    Pending pending_ = Pending::None;

    SavePath nvram_path_{};
    SavePath card_path_{};
};

inline constexpr Cr511Drive::DriveFlags Cr511Drive::DriveFlags::kNotReady{1u << 0};
inline constexpr Cr511Drive::DriveFlags Cr511Drive::DriveFlags::kPlaying{1u << 2};
inline constexpr Cr511Drive::DriveFlags Cr511Drive::DriveFlags::kFinished{1u << 3};
inline constexpr Cr511Drive::DriveFlags Cr511Drive::DriveFlags::kError{1u << 4};
inline constexpr Cr511Drive::DriveFlags Cr511Drive::DriveFlags::kMotor{1u << 5};
inline constexpr Cr511Drive::DriveFlags Cr511Drive::DriveFlags::kMedia{1u << 6};

}
