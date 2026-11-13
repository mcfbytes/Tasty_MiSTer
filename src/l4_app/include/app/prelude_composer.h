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

#include "app/audio_pump.h"
#include "app/prelude_stats.h"
#include "app/staged_frame.h"
#include "app/video_wire.h"
#include "infra/error.h"
#include "infra/seat.h"
#include "infra/telemetry.h"
#include "os/clock.h"
#include "os/raw_monotonic_clock.h"
#include "proto/link_event.h"
#include "proto/types.h"
#include "svc/filter_send_plan.h"
#include "svc/filter_set.h"
#include "svc/video_service.h"

namespace mister::app {

class PreludeComposer : public svc::VideoService::IVideoWireSink {
    TASTY_SEAT_RESIDENT(Ui);

public:
    struct Wiring {
        VideoWire& wire;
        AudioPump& audio;
        const os::IClock& clock;
    };

    struct Answers {
        PreludeCompletions completions{};
        proto::ReplyWord hdmi_int{};
    };

    struct Inputs {
        ArCustomWords ar_custom{};
        svc::ShadowMaskWord shadow{};

        std::string_view core{};
        std::string_view afilter_default{};
    };

    static constexpr std::chrono::milliseconds kReplyDeadline{1000};

    explicit PreludeComposer(Wiring w) noexcept : w_(w) {}

    PreludeComposer(const PreludeComposer&) = delete;
    PreludeComposer& operator=(const PreludeComposer&) = delete;

    void arm(bool front_end) noexcept;

    void begin(const Inputs& in) noexcept;

    void take_filters(std::unique_ptr<svc::FilterSet> set) noexcept;

    [[nodiscard]] Ex<void> publish(proto::UioOp op, std::span<const std::uint16_t> w) override;

    void on_reply(const proto::LinkEvent::FrameReply& r) noexcept;

    [[nodiscard]] bool owns(proto::StageGen gen) const noexcept;

    void service() noexcept;

    void abandon() noexcept;

    [[nodiscard]] bool holding_mode() const noexcept;

    [[nodiscard]] Answers answers() const noexcept { return answers_; }

    [[nodiscard]] const xthread::Telemetry<PreludeStats, SeatTag::Ui>& stats_cell() const noexcept {
        return stats_cell_;
    }

private:
    struct Publish {
        std::span<const std::uint16_t> v{};

        [[nodiscard]] bool fits_block() const noexcept {
            return !v.empty() && v.size() <= VideoWire::kWords;
        }

        [[nodiscard]] FrameWordCount count() const noexcept {
            const std::uint8_t words = static_cast<std::uint8_t>(v.size());
            return FrameWordCount{words};
        }

        [[nodiscard]] std::uint32_t detail() const noexcept {
            const std::uint32_t words = static_cast<std::uint32_t>(v.size());
            return words;
        }

        [[nodiscard]] std::span<const std::uint16_t> words() const noexcept { return v; }
    };

    struct RepliesOwed {
        std::uint8_t v = 0;

        [[nodiscard]] constexpr bool is_set() const noexcept { return v != 0; }

        constexpr void settle_one() noexcept {
            if (v != 0)
                --v;
        }

        friend constexpr bool operator==(RepliesOwed, RepliesOwed) = default;
    };

    struct HeldCount {
        std::size_t v = 0;

        constexpr void add_one() noexcept { ++v; }

        [[nodiscard]] constexpr std::uint32_t stats_word() const noexcept {
            const std::uint32_t parked = static_cast<std::uint32_t>(v);
            return parked;
        }

        friend constexpr bool operator==(HeldCount, HeldCount) = default;
    };

    struct ChunkWords {
        std::size_t v = 0;

        [[nodiscard]] FrameWordCount frame_words() const noexcept {
            const std::uint8_t words = static_cast<std::uint8_t>(v);
            return FrameWordCount{words};
        }

        [[nodiscard]] std::uint32_t stats_word() const noexcept {
            const std::uint32_t words = static_cast<std::uint32_t>(v);
            return words;
        }

        [[nodiscard]] svc::FilterBankWordIndex past(svc::FilterBankWordIndex from) const noexcept {
            const std::size_t place = from.wire_offset(svc::FilterAddressGrid{});
            return svc::FilterBankWordIndex{place + v};
        }

        template <class WordAt>
        void fill(
            std::span<std::uint16_t> block,
            svc::FilterBankWordIndex from,
            WordAt&& word_at
        ) const {
            const std::size_t base = from.wire_offset(svc::FilterAddressGrid{});
            for (std::size_t i = 0; i < v; ++i)
                block[i] = word_at(svc::FilterBankWordIndex{base + i});
        }

        friend constexpr bool operator==(ChunkWords, ChunkWords) = default;
    };

    struct FilterReply {
        std::uint16_t v = 0;

        [[nodiscard]] static FilterReply of(proto::ReplyWord word) noexcept {
            const std::uint16_t bits = word.v;
            return FilterReply{bits};
        }

        [[nodiscard]] svc::FltFlags flags() const noexcept {
            const std::uint16_t bits = v;
            return svc::FltFlags{bits};
        }
    };

    static constexpr RepliesOwed kFbufProbesReplies{3};

    struct Held {
        proto::UioOp op{};
        FrameWordCount n{};
        std::array<std::uint16_t, VideoWire::kWords> w{};
        bool live = false;

        [[nodiscard]] std::span<const std::uint16_t> words() const noexcept {
            return n.prefix_of(w);
        }
    };

    struct InFlight {
        proto::StageGen gen{};
        StagedFrame::Kind kind{};
        RepliesOwed remaining{};
    };
    enum class Release : std::uint8_t { None, ModeInBlock, ShadowMask, AudioTail, Modes, Settle };

    [[nodiscard]] os::RawMonotonicClock::time_point now() const noexcept { return w_.clock.now(); }

    [[nodiscard]] Ex<void> stage_(
        const StagedFrame& f,
        StagedFrame::Kind kind,
        RepliesOwed replies
    ) noexcept;
    [[nodiscard]] InFlight* find_flight_(proto::StageGen gen) noexcept;
    [[nodiscard]] Held* find_held_(proto::UioOp op) noexcept;
    [[nodiscard]] const Held* find_held_(proto::UioOp op) const noexcept;
    [[nodiscard]] HeldCount held_count_() const noexcept;
    void hold_(proto::UioOp op, Publish words) noexcept;
    void publish_stats_() noexcept;
    void release_step_() noexcept;
    void begin_release_() noexcept;
    void finish_block_() noexcept;
    void service_stream_() noexcept;
    void service_wheel_() noexcept;
    void on_fltnum_(const proto::LinkEvent::FrameReply& r) noexcept;

    Wiring w_;
    PreludeStep step_ = PreludeStep::Idle;
    bool bound_ok_ = false;
    proto::BindGeneration bound_{};
    bool front_end_ = false;
    bool began_ = false;
    bool fbuf_owed_ = false;
    bool filter_owed_ = false;
    Inputs inputs_{};
    std::unique_ptr<svc::FilterSet> set_{};

    svc::FilterSendPlan plan_{};
    svc::FilterFormat fmt_{};
    svc::VFilter vert_ = svc::VFilter::Horz;
    bool bare_ = false;
    bool bare_no_digests_ = false;
    svc::FilterSendPlan::SegIndex seg_{};
    svc::FilterBankWordIndex cursor_{};
    svc::FilterBank sent_h_{};
    svc::FilterBank sent_v_{};

    ResendSeq last_filter_seq_{};
    ResendSeq last_scaling_seq_{};
    bool wheel_active_ = false;
    bool pair_owed_ = false;

    std::array<Held, 8> held_{};
    std::array<InFlight, VideoWire::kFramesInFlight> flight_{};

    Release release_ = Release::None;
    bool mode_in_block_done_ = false;
    bool shadow_done_ = false;
    bool tail_done_ = false;
    std::optional<os::RawMonotonicClock::time_point> deadline_{};

    Answers answers_{};
    PreludeStats stats_{};
    xthread::Telemetry<PreludeStats, SeatTag::Ui> stats_cell_{};
};

}
