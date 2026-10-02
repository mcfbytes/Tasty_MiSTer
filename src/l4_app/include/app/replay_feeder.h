// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "app/conf_str_cell.h"
#include "app/path_text.h"
#include "app/replay_control.h"
#include "app/replay_msg.h"
#include "app/replay_status.h"
#include "app/session_identity.h"
#include "cores/movie_codec.h"
#include "cores/rom_digest.h"
#include "infra/seat.h"
#include "os/clock.h"
#include "proto/status_cell.h"

namespace mister::svc {
class IFile;
class Vfs;
}  // namespace mister::svc
namespace mister::xthread {
class DiagLog;
}
namespace mister::proto {
class ItemTable;
}

namespace mister::app {

class IdentityLatch;
class InputWire;
class IOsdClose;
class LinkTxChannel;
class ScreenshotPump;
class UiRequestRing;
class VideoPump;

class ReplayFeeder {
    TASTY_SEAT_RESIDENT(Ui);

public:
    struct Wiring {
        const svc::Vfs* vfs = nullptr;
        const os::IClock* clock = nullptr;
        ReplayRing* ring = nullptr;
        ReplayControlCell* control = nullptr;
        const ReplayStatusCell* status = nullptr;
        const IdentityLatch* identity = nullptr;
        const proto::StatusCell* core_status = nullptr;
        const ConfStrCell* conf = nullptr;
        UiRequestRing* asks = nullptr;
        const VideoPump* video = nullptr;
        InputWire* input = nullptr;
        ScreenshotPump* shots = nullptr;
        xthread::DiagLog* diag = nullptr;

        IOsdClose* osd = nullptr;

        LinkTxChannel* settings_tx = nullptr;
    };

    enum class Stage : std::uint8_t {
        Idle,
        Scanning,
        Hashing,
        Settling,
        Arming,
        Loading,
        Streaming,
        Stopping
    };

    enum class Refusal : std::uint8_t {
        None,
        Busy,
        NotWired,
        NoCodec,
        MovieIo,
        Movie,
        RomIo,
        RomSize,
        Checksum,
        Setting,
        Phase,
        Lead,
        Command,
        TooLong,
        Slot,
        PowerOn,
        Firmware,
        RomKind,
        RomChd,
        kCount,
    };

    struct Play {
        std::string_view movie{};
        std::string_view rom{};
        std::optional<std::uint32_t> phase_us{};
        std::optional<std::int32_t> lead{};

        std::optional<std::uint32_t> stop_at{};

        bool set_settings = false;

        std::optional<cores::IMovieCodec::RamFill> ram_fill{};

        bool seeded_save = false;
    };

    struct SetSetting {
        std::string_view name{};
        std::string was{};
        std::string now{};
    };

    static constexpr std::uint32_t kMaxFrames = 1u << 20;
    static constexpr std::uint32_t kPhaseMinUs = 1000;
    static constexpr std::uint32_t kPhaseGuardUs = 2000;

    [[nodiscard]] static constexpr bool phase_fits(std::uint32_t phase_us,
                                                   std::uint64_t period_ns) noexcept {
        const std::uint64_t period_us = period_ns / 1000;
        return period_us > kPhaseGuardUs && phase_us >= kPhaseMinUs &&
               phase_us <= period_us - kPhaseGuardUs;
    }

    static constexpr std::uint32_t kRunSplit = 60;
    static constexpr std::uint32_t kReadChunk = 16 * 1024;
    static constexpr std::uint32_t kTickBudget = 64 * 1024;
    static constexpr unsigned kEpochTries = 3;
    static constexpr std::size_t kCheckpoints = 64;
    static constexpr std::int64_t kAnswerNs = 2'000'000'000;
    static constexpr std::int64_t kPowerOnNs = 20'000'000'000;

    explicit ReplayFeeder(const Wiring& w) noexcept;
    ~ReplayFeeder();
    ReplayFeeder(const ReplayFeeder&) = delete;
    ReplayFeeder& operator=(const ReplayFeeder&) = delete;

    bool take_play(const Play& p) noexcept;
    bool take_stop() noexcept;

    void tick() noexcept;

    [[nodiscard]] Stage stage() const noexcept { return stage_; }
    [[nodiscard]] std::uint16_t gen() const noexcept { return gen_; }
    [[nodiscard]] Refusal last_refusal() const noexcept { return last_refusal_; }

    [[nodiscard]] std::uint32_t last_refusal_detail() const noexcept { return last_detail_; }
    [[nodiscard]] std::string_view last_setting() const noexcept { return last_setting_; }

    [[nodiscard]] bool last_setting_offered() const noexcept { return last_setting_offered_; }

    [[nodiscard]] std::string_view refusal_path() const noexcept { return refusal_path_; }
    [[nodiscard]] std::uint32_t refusals() const noexcept { return refusals_; }
    [[nodiscard]] std::uint32_t frames() const noexcept { return frames_; }
    [[nodiscard]] std::uint32_t movie_frames() const noexcept { return movie_frames_; }
    [[nodiscard]] std::uint32_t tries() const noexcept { return tries_; }
    [[nodiscard]] std::span<const SetSetting> settings_set() const noexcept { return set_; }
    [[nodiscard]] static const char* refusal_name(Refusal r) noexcept;

private:
    struct Line {
        std::array<char, cores::IMovieCodec::kLineMax + 1> text{};
        std::uint16_t len = 0;
        bool clipped = false;
        std::uint64_t start = 0;
    };
    struct Run {
        std::uint32_t first = 0;
        std::uint32_t last = 0;
        std::array<std::uint32_t, kReplayPorts> mask{};
    };

    void refuse_(Refusal why, std::uint32_t detail = 0) noexcept;
    void log_end_(const ReplayStatus& s) noexcept;
    [[nodiscard]] bool read_lines_(bool scan) noexcept;
    [[nodiscard]] bool on_line_(std::string_view line, bool clipped, std::uint64_t start,
                                bool scan) noexcept;
    [[nodiscard]] bool scan_line_(std::string_view line, bool clipped,
                                  std::uint64_t start) noexcept;
    [[nodiscard]] bool stream_line_(std::string_view line) noexcept;
    void finish_scan_() noexcept;
    void hash_rom_() noexcept;
    void finish_rom_() noexcept;
    void settle_() noexcept;

    [[nodiscard]] bool set_unmet_(const proto::StatusWord& live,
                                  const cores::IMovieCodec::SettingNeeds& needs,
                                  const proto::ItemTable* labels) noexcept;
    [[nodiscard]] std::optional<std::uint8_t> rom_slot_() noexcept;
    void arm_() noexcept;
    void tick_arming_(const ReplayStatus& s, bool fresh) noexcept;
    void tick_running_(const ReplayStatus& s, bool fresh) noexcept;
    [[nodiscard]] bool flush_() noexcept;
    [[nodiscard]] bool push_run_() noexcept;
    void checkpoints_(const ReplayStatus& s) noexcept;
    void load_checkpoints_() noexcept;
    [[nodiscard]] std::optional<ReplayStatus> sample_status_() const noexcept;
    void publish_(ReplayOp op) noexcept;
    void finish_(const ReplayStatus& s) noexcept;
    [[nodiscard]] std::int64_t now_ns_() const noexcept;

    Wiring w_;
    Stage stage_ = Stage::Idle;
    std::uint16_t gen_ = 0;
    const cores::IMovieCodec* codec_ = nullptr;
    cores::IMovieCodec::Facts facts_{};
    PathText movie_{};
    PathText rom_{};
    std::uint32_t offset_us_ = 0;
    std::optional<std::int32_t> lead_override_{};
    std::optional<std::uint32_t> stop_at_{};
    std::optional<cores::IMovieCodec::RamFill> ram_fill_{};
    bool seeded_save_ = false;
    bool set_ok_ = false;
    std::vector<SetSetting> set_{};
    std::int32_t lead_ = 0;
    std::uint32_t poweron_ns_ = 0;
    ReplayMsg::P0Parity p0_ = ReplayMsg::P0Parity::Any;
    ReplayMsg::PowerOnEvent event_ = ReplayMsg::PowerOnEvent::LoadEnd;
    bool vsync_ok_ = false;

    std::unique_ptr<svc::IFile> file_{};
    std::uint64_t file_size_ = 0;

    bool stream_cut_ = false;

    std::unique_ptr<svc::IFile> rom_file_{};
    std::uint64_t rom_off_ = 0;
    std::uint64_t rom_end_ = 0;
    std::uint8_t rom_index_ = 0xFF;
    cores::RomDigest digest_{};
    cores::RomDigest digest_alt_{};
    bool has_alt_ = false;
    bool firmware_pass_ = false;
    std::uint64_t off_ = 0;
    std::uint64_t log_start_ = 0;
    bool in_log_ = false;
    bool log_done_ = false;
    Line line_{};

    std::uint32_t frames_ = 0;
    std::uint32_t movie_frames_ = 0;
    std::optional<Run> run_{};
    std::optional<Run> next_{};
    bool end_pushed_ = false;
    unsigned tries_ = 0;
    std::int64_t since_ns_ = 0;
    bool stop_asked_ = false;

    std::array<std::uint32_t, kCheckpoints> checks_{};
    std::size_t n_checks_ = 0;
    std::size_t next_check_ = 0;

    SessionIdentity ident_{};
    ConfStrText conf_{};
    Refusal last_refusal_ = Refusal::None;
    std::uint32_t last_detail_ = 0;
    std::string_view last_setting_{};
    bool last_setting_offered_ = false;
    std::string refusal_path_{};
    std::uint32_t refusals_ = 0;
};

}  // namespace mister::app
