// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>

#include "cores/mailbox_servant.h"
#include "cores/minimig/akiko_drive_status.h"
#include "cores/minimig/akiko_opcode.h"
#include "cores/minimig/akiko_prefetch.h"
#include "cores/minimig/akiko_subcode.h"
#include "cores/minimig/akiko_toc.h"
#include "cores/minimig/akiko_wire.h"
#include "cores/minimig/cd_disc.h"
#include "cores/minimig/cd_status.h"
#include "cores/nvram_store.h"
#include "hal/types.h"
#include "infra/byte_count.h"
#include "infra/fixed_ring.h"
#include "infra/seat.h"
#include "proto/mailbox_act.h"
#include "proto/mailbox_result.h"
#include "proto/types.h"
#include "svc/card_writer.h"
#include "svc/types.h"
#include "svc/vfs.h"

namespace mister::cores::minimig::akiko {

enum class Handshake : std::uint8_t { Fresh, Greeted, Identified };

enum class PlayEnd : std::uint8_t { Error, Playing, Ended };

[[nodiscard]] constexpr bool stops_play(PlayEnd state) noexcept {
    return state != PlayEnd::Playing;
}

struct Passes {
    std::uint32_t v = 0;
    using affine_count = Passes;

    [[nodiscard]] constexpr Passes next() const noexcept { return Passes{v + 1}; }

    friend constexpr bool operator==(Passes, Passes) = default;
    friend constexpr auto operator<=>(Passes, Passes) = default;
};

inline constexpr Passes kAudioHold{50};
inline constexpr Passes kReadRetry{8};
inline constexpr Passes kPlayStartPasses{2};

struct AudioTimeout {
    TASTY_SEAT_EXEMPT(component);
    std::int8_t v = 0;

    static const AudioTimeout kIdle;
    static const AudioTimeout kEnded;
    static const AudioTimeout kEndedSettle;
    static const AudioTimeout kFailed;
    static const AudioTimeout kLastPlayingPass;

    [[nodiscard]] static constexpr AudioTimeout countdown(Passes passes) noexcept {
        return AudioTimeout{static_cast<std::int8_t>(passes.v)};
    }

    [[nodiscard]] constexpr bool armed() const noexcept { return v != kIdle.v; }

    [[nodiscard]] constexpr std::optional<PlayEnd> step() noexcept {
        if (v > kLastPlayingPass.v) {
            --v;
            return std::nullopt;
        }
        if (v == kLastPlayingPass.v) {
            v = kIdle.v;
            return PlayEnd::Playing;
        }
        if (v == kEnded.v) {
            v = kEndedSettle.v;
            return std::nullopt;
        }
        if (v == kEndedSettle.v) {
            v = kIdle.v;
            return PlayEnd::Ended;
        }
        v = kIdle.v;
        return PlayEnd::Error;
    }

    constexpr void cancel_countdown() noexcept {
        if (v > kIdle.v)
            v = kIdle.v;
    }

    friend constexpr bool operator==(AudioTimeout, AudioTimeout) = default;
};

inline constexpr AudioTimeout AudioTimeout::kIdle{0};
inline constexpr AudioTimeout AudioTimeout::kEnded{-1};
inline constexpr AudioTimeout AudioTimeout::kEndedSettle{-2};
inline constexpr AudioTimeout AudioTimeout::kFailed{-3};
inline constexpr AudioTimeout AudioTimeout::kLastPlayingPass{1};

struct SectorCounter {
    std::uint8_t v = 0;

    [[nodiscard]] static constexpr SectorCounter of(hal::SpiWord word) noexcept {
        return SectorCounter{std::to_integer<std::uint8_t>(word.low_lane())};
    }

    [[nodiscard]] constexpr proto::BlockCount offset() const noexcept {
        return proto::BlockCount{v};
    }

    friend constexpr bool operator==(SectorCounter, SectorCounter) = default;
};

struct StuckSector {
    TASTY_SEAT_EXEMPT(component);
    std::optional<proto::Lba> at{};
    Passes passes{};

    [[nodiscard]] constexpr bool hit(proto::Lba lba, Passes limit) noexcept {
        if (!at || *at != lba) {
            at = lba;
            passes = Passes{};
        }
        passes = passes.next();
        if (passes < limit)
            return false;
        at.reset();
        passes = Passes{};
        return true;
    }
};

}

namespace mister::cores::minimig {

class AkikoDrive {
    TASTY_SEAT_RESIDENT(Pcm);

public:
    using BulkBytes = IMailboxServant::BulkBytes;
    using Instant = proto::MailboxAct::NotBefore;

    static constexpr std::size_t kNvramBytes = 1024;

    static constexpr std::chrono::milliseconds kSwapSettle{700};

    static constexpr std::chrono::milliseconds kTocPeriod{20};

    static constexpr std::chrono::milliseconds kVerifyAfter{100};

    static constexpr std::chrono::milliseconds kPass{1};

    enum class Pending : std::uint8_t { None, Write, Audio, NvramDump };

    explicit AkikoDrive(const svc::Vfs& vfs) : vfs_(vfs), writer_(vfs) {}

    void init() noexcept;

    void forget() noexcept;

    void set_active(bool on) noexcept { active_ = on; }

    void bind(std::optional<CdDisc> disc, std::string_view path) noexcept;

    void on_status(CdStatus status) noexcept { status_ = status; }

    void on_command(std::span<const std::byte> bytes) noexcept;
    void on_sector_counter(akiko::SectorCounter counter) noexcept;

    void on_osd(bool shown) noexcept;

    void settled(const proto::MailboxResult& result, std::span<const std::byte> bulk) noexcept;

    [[nodiscard]] proto::MailboxAct next(Instant now, BulkBytes bulk) noexcept;

    [[nodiscard]] std::optional<Instant> deadline(Instant now) const noexcept;

    [[nodiscard]] Pending pending() const noexcept { return pending_; }

    [[nodiscard]] akiko::Handshake initialized() const noexcept { return initialized_; }

    [[nodiscard]] bool playing() const noexcept { return playing_; }

    [[nodiscard]] std::string_view save_path() const noexcept { return save_path_.view(); }

    [[nodiscard]] bool save_load_failed() const noexcept { return save_load_failed_; }

    [[nodiscard]] const akiko::AkikoToc& toc() const noexcept { return toc_; }

private:
    enum class Media : std::uint8_t { Unknown, Absent, Present };

    enum class Dump : std::uint8_t { Verify, Save, Flush };

    struct Command {
        akiko::Frame bytes{};
        infra::ByteCount len{};
    };

    struct CddaPlay {
        proto::Lba next{};
        proto::Lba end{};
    };

    static constexpr std::size_t kQueued = 4;

    void media_edge_() noexcept;
    [[nodiscard]] proto::MailboxAct saves_(Instant now, BulkBytes bulk) noexcept;
    [[nodiscard]] proto::MailboxAct pushes_(Instant now, BulkBytes bulk) noexcept;
    [[nodiscard]] proto::MailboxAct cdda_(BulkBytes bulk) noexcept;
    [[nodiscard]] proto::MailboxAct sector_(BulkBytes bulk) noexcept;
    [[nodiscard]] proto::MailboxAct command_(BulkBytes bulk) noexcept;

    void dispatch_(const Command& c) noexcept;
    void cmd_stop_(const Command& c) noexcept;
    void cmd_pause_(const Command& c, bool pause) noexcept;
    void cmd_multi_(const Command& c) noexcept;
    void cmd_led_(const Command& c) noexcept;
    void cmd_subq_(const Command& c) noexcept;
    void cmd_info_(const Command& c) noexcept;
    void cmd_bad_(const Command& c, akiko::DriveStatus err) noexcept;
    void playend_(akiko::PlayEnd state) noexcept;

    void reply_(std::span<const std::byte> payload) noexcept;

    [[nodiscard]] bool mounted_() const noexcept { return active_ && disc_.has_value(); }

    [[nodiscard]] bool present_() const noexcept { return media_ == Media::Present; }

    [[nodiscard]] std::span<const Track> tracks_() const noexcept;
    [[nodiscard]] svc::TrackCount real_tracks_() const noexcept;
    void disarm_cdda_() noexcept;

    [[nodiscard]] proto::MailboxAct load_nvram_(
        std::string_view path,
        Instant now,
        BulkBytes bulk
    ) noexcept;
    [[nodiscard]] proto::MailboxAct dump_(Dump why, std::string_view path) noexcept;
    void dumped_(std::span<const std::byte> image) noexcept;
    [[nodiscard]] bool save_(std::string_view path, std::span<const std::byte> image) noexcept;

    [[nodiscard]] proto::MailboxAct written_(proto::MailboxAct act) noexcept;

    [[nodiscard]] proto::MailboxAct reply_act_(BulkBytes bulk) noexcept;

    const svc::Vfs& vfs_;
    svc::CardWriter writer_;
    std::optional<CdDisc> disc_;
    akiko::AkikoPrefetch prefetch_;
    akiko::AkikoToc toc_;
    CdStatus status_{};
    bool active_ = false;

    akiko::Handshake initialized_ = akiko::Handshake::Fresh;
    bool paused_ = false;
    bool playing_ = false;
    bool led_on_ = false;
    proto::Lba play_start_{};
    std::optional<proto::Lba> data_base_{};
    akiko::AudioTimeout audio_timeout_{};
    std::optional<proto::Lba> cdda_q_pos_{};
    std::optional<proto::Lba> cdda_q_end_{};
    std::optional<CddaPlay> cdda_play_{};
    Media media_ = Media::Unknown;
    bool media_push_pending_ = false;
    bool media_event_ = false;
    bool swap_absent_ = false;
    std::optional<Instant> swap_ready_{};
    std::optional<Instant> toc_last_{};

    std::optional<akiko::SectorCounter> counter_{};
    akiko::StuckSector held_audio_{};
    akiko::StuckSector read_retry_{};

    infra::FixedRing<Command, kQueued> commands_{};
    std::optional<Command> reply_owed_{};
    std::optional<akiko::Subcode> subcode_owed_{};
    Pending pending_ = Pending::None;

    SavePath save_path_{};
    bool save_load_pending_ = false;
    bool save_dirty_observed_ = false;
    bool save_load_failed_ = false;
    bool osd_was_shown_ = false;
    bool osd_edge_ = false;
    std::optional<Instant> verify_due_{};
    SavePath verify_path_{};
    std::optional<SavePath> flush_to_{};
    Dump dump_for_ = Dump::Verify;
    SavePath dump_path_{};
};

}
