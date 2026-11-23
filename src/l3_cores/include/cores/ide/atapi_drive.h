// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "cores/ide/atapi_packet.h"
#include "cores/ide/atapi_sense.h"
#include "cores/ide/atapi_step.h"
#include "cores/ide/atapi_volume.h"
#include "infra/byte_count.h"
#include "infra/run.h"
#include "infra/seat.h"
#include "proto/types.h"
#include "svc/disc/disc_reader.h"
#include "svc/disc/toc.h"
#include "svc/disc/track.h"

namespace mister::cores::ide {

class AtapiDrive final {
    TASTY_SEAT_RESIDENT(Io);

public:
    static constexpr proto::BlockSize kBlock{svc::disc::kUserDataBytes};
    static constexpr proto::BlockCount kBurstBlocks{8};
    static constexpr infra::ByteCount kBlockBytes{svc::disc::kUserDataBytes};
    static constexpr infra::ByteCount kBuffer{kBlock.window_bytes(kBurstBlocks)};

    static constexpr infra::ByteCount kParameterBytes{512};

    static constexpr proto::Lba kCurrentPosition{0xFFFF'FFFF};

    struct Disc {
        const svc::disc::Toc& toc;
        svc::disc::IDiscReader& reader;
    };

    struct Volumes {
        AtapiVolume port0{};
        AtapiVolume port1{};
        friend constexpr bool operator==(Volumes, Volumes) = default;
    };

    AtapiDrive() noexcept = default;
    AtapiDrive(const AtapiDrive&) = delete;
    AtapiDrive& operator=(const AtapiDrive&) = delete;

    void insert(Disc disc) noexcept;
    void remove() noexcept;

    void power_on() noexcept;

    void bus_reset() noexcept;

    void begin_packet(infra::ByteCount limit) noexcept;
    [[nodiscard]] AtapiStep packet(const AtapiPacket& cmd) noexcept;

    [[nodiscard]] AtapiStep data_in_done() noexcept;

    [[nodiscard]] AtapiStep data_out(std::span<const std::byte> parameters) noexcept;

    [[nodiscard]] std::span<const std::byte> reply() const noexcept;

    [[nodiscard]] std::span<const std::byte> reply_words() const noexcept;

    [[nodiscard]] bool playing() const noexcept { return play_.state == PlayState::Playing; }

    [[nodiscard]] bool play_frame(std::span<std::byte, svc::disc::kRawFrameBytes> frame) noexcept;

    [[nodiscard]] Volumes volumes() const noexcept { return volumes_; }

private:
    enum class Load : std::uint8_t { Ready, Settling, SpinningUp, NoDisc };

    enum class PlayState : std::uint8_t { Stopped, Playing, Paused, Parked };

    enum class Attention : std::uint8_t { Report, Hold };

    struct Play {
        PlayState state = PlayState::Stopped;
        proto::Lba head{};
        proto::Lba end{};
    };

    struct PlayEnds {
        proto::Lba from{};
        proto::Lba to{};
    };

    [[nodiscard]] static constexpr Load settled_(Load load) noexcept;
    [[nodiscard]] AtapiStep complete_(
        AtapiSense sense,
        Attention attention = Attention::Report,
        bool seek_complete = false
    ) noexcept;

    [[nodiscard]] AtapiStep good_() noexcept { return complete_(AtapiSense{}); }

    [[nodiscard]] AtapiStep refuse_(AtapiAsc why) noexcept;
    [[nodiscard]] AtapiStep not_ready_() noexcept;
    [[nodiscard]] AtapiStep send_(infra::ByteCount bytes) noexcept;
    [[nodiscard]] AtapiStep read_blocks_(infra::affine::Run<proto::Lba> run) noexcept;
    [[nodiscard]] AtapiStep serve_read_() noexcept;
    void fill_block_(proto::Lba block, std::span<std::byte> user_data) noexcept;
    [[nodiscard]] infra::ByteCount answer_sense_() noexcept;
    [[nodiscard]] AtapiStep answer_track_information_(const AtapiPacket& cmd) noexcept;
    [[nodiscard]] AtapiStep answer_subchannel_(const AtapiPacket& cmd) noexcept;
    [[nodiscard]] AtapiStep pause_resume_(const AtapiPacket& cmd) noexcept;
    void start_play_(PlayEnds ends) noexcept;

    [[nodiscard]] const svc::disc::Toc& table_() const noexcept;

    std::optional<Disc> disc_{};
    Load load_ = Load::NoDisc;
    bool swapped_ = false;
    AtapiSense sense_{};
    Play play_{};
    Volumes volumes_{};
    infra::affine::Run<proto::Lba> read_{};
    infra::ByteCount limit_{kBuffer};
    infra::ByteCount reply_len_{};
    std::array<std::byte, kBuffer.size()> buf_{};
};

}
