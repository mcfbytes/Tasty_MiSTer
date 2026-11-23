// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <span>
#include <vector>

#include "cores/atari8/atari8_status3.h"
#include "cores/atari8/cas_types.h"
#include "cores/types.h"
#include "hal/spi_halves.h"
#include "infra/endian.h"
#include "infra/seat.h"
#include "proto/mailbox_act.h"
#include "proto/mailbox_result.h"
#include "proto/mailbox_run.h"
#include "proto/types.h"
#include "svc/file.h"

namespace mister::cores::atari8 {

class CasFeed {
    TASTY_SEAT_RESIDENT(Pcm);

public:
    using ActBytes = std::span<std::uint8_t, proto::kMailboxActBytes>;
    using NotBefore = proto::MailboxAct::NotBefore;

    struct Pulse {
        hal::SpiHalves entry{};
        friend constexpr bool operator==(const Pulse&, const Pulse&) = default;
    };

    enum class After : std::uint8_t { More, Gap, End };

    class Walk {
    public:
        [[nodiscard]] bool open(std::vector<std::byte> file, Atari8Status3 status3);

        [[nodiscard]] After block();

        [[nodiscard]] std::vector<Pulse> take() noexcept;

    private:
        enum class Chunk : std::uint8_t { Fuji, Baud, Data, Fsk, Pwms, Pwmc, Pwmd, Pwml, Other };

        struct Header {
            Chunk kind = Chunk::Other;
            proto::FileSize length{};
            std::array<std::byte, 2> aux{};

            [[nodiscard]] constexpr TapeRate rate() const noexcept {
                return TapeRate{infra::load_le<std::uint16_t>(std::span(aux))};
            }

            [[nodiscard]] constexpr TapeSilence silence() const noexcept {
                return TapeSilence{infra::load_le<std::uint16_t>(std::span(aux))};
            }

            [[nodiscard]] constexpr std::optional<PwmSettings> pwm() const noexcept {
                return PwmSettings::of(aux[0]);
            }

            [[nodiscard]] constexpr std::byte pwm_width(TapeLevel level) const noexcept {
                return level == TapeLevel::Low ? aux[0] : aux[1];
            }
        };

        [[nodiscard]] static Chunk chunk_of_(std::span<const std::byte> signature) noexcept;
        [[nodiscard]] std::optional<Header> header_at_(proto::FileOffset at) const noexcept;
        void emit_(TapeLevel level, TapeTicks ticks);

        [[nodiscard]] std::byte byte_(proto::FileOffset at) const noexcept;
        [[nodiscard]] std::uint16_t word_(proto::FileOffset at) const noexcept;

        [[nodiscard]] bool holds_(proto::FileOffset at, proto::FileSize n) const noexcept;
        [[nodiscard]] proto::FileOffset end_() const noexcept;

        [[nodiscard]] std::optional<proto::FileOffset> forward_(proto::FileOffset at);

        std::vector<std::byte> file_;
        std::vector<Pulse> out_;
        ClockHz core_hz_{};
        Header header_{};
        TapeTicks pwm_sample_{};
        TapeTicks sample_{};
        TapeSilence silence_{};
        proto::FileOffset offset_{};
        std::optional<TapeMode> mode_{};
        std::optional<TapeMode> prev_mode_{};
        proto::FileSize walked_{};
        proto::FileSize group_{};
        TapeLevel fsk_bit_ = TapeLevel::Low;
        TapeLevel pwm_bit_ = TapeLevel::Low;
        BitOrder pwm_bit_order_ = BitOrder::LsbFirst;
    };

    enum class Step : std::uint8_t { Idle, Reset, Pulses, Keys, Motor, Drain, Pause };

    void begin_core() noexcept;

    void mount(std::unique_ptr<svc::IFile> file, bool boot) noexcept;

    void unmount() noexcept;

    [[nodiscard]] proto::MailboxAct next(ActBytes bytes, NotBefore now) noexcept;
    void settled(const proto::MailboxResult& result, ActBytes bytes, NotBefore now) noexcept;

    [[nodiscard]] Step step() const noexcept { return step_; }

    static constexpr proto::ReplyCount kPulsesPerRun{12};

    static constexpr std::chrono::milliseconds kFullRetry{1};
    static constexpr std::chrono::milliseconds kMotorPoll{5};

    static constexpr std::chrono::microseconds kSlowPause{400'000};
    static constexpr std::chrono::microseconds kPause{10'000};

    static constexpr proto::FileSize kMaxTapeBytes{std::uint64_t{8} << 20};

private:
    [[nodiscard]] proto::MailboxAct run_(ActBytes bytes, NotBefore at) noexcept;
    void after_pulses_() noexcept;
    void walk_block_() noexcept;

    Walk walk_{};
    std::unique_ptr<svc::IFile> file_{};
    Step step_ = Step::Idle;
    bool boot_ = false;
    bool open_ = false;
    After after_ = After::More;
    std::deque<Pulse> pending_{};
    NotBefore at_{};
    std::chrono::microseconds pause_ = kPause;
};

}
