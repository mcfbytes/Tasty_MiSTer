// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <tuple>
#include <type_traits>

#include "app/link_op_facts.h"
#include "app/types.h"
#include "hal/types.h"
#include "infra/message_sum.h"
#include "proto/uio_opcodes.h"
#include "proto/user_io/declared_select.h"
#include "svc/video/scaler_slot_mode.h"
#include "svc/video/scaling_words.h"
#include "svc/video/shadow_mask_word.h"
#include "svc/types.h"
#include "svc/video/video_wire_emit.h"

namespace mister::app {

inline constexpr std::size_t kArCustomWords = 4;

using ArCustomWords = std::array<hal::LinkWord, kArCustomWords>;

inline constexpr std::size_t kCoeffChunkWords = 64;

enum class AfilterPushForm : std::uint8_t { Withheld, Byte, Stream };

enum class FbufWindow : std::uint8_t { Fbuf, HdmiProbe, GammaProbe, ArCustom, kCount };

enum class TailWindow : std::uint8_t { Probe, Flush, Push, AudVol, ButSw, kCount };

enum class FrameWindow : std::uint8_t { FltNum, ShadowCap, ModeEmitted, kCount };

struct StagedFrame {
    enum class Kind : std::uint8_t {
        Mode,
        ModeInBlock,
        FbufProbes,
        FilterMode,
        CoeffChunk,
        ShadowMask,
        AudioTail,
        ScalingPair,
        AudVol,
        AfilterPush,
        kCount,
    };

    struct ModeWords {
        proto::UioOp opcode{};
        FrameWordCount count{};
        std::byte pad_{};
        std::array<hal::LinkWord, svc::video::kVideoWireWords> w{};
    };

    struct Mode {
        static constexpr Kind kKind = Kind::Mode;
        ModeWords m;
    };

    struct ModeInBlock {
        static constexpr Kind kKind = Kind::ModeInBlock;
        ModeWords m;
    };

    struct FbufProbes {
        static constexpr Kind kKind = Kind::FbufProbes;
        ArCustomWords ar_custom{};
    };

    struct FilterMode {
        static constexpr Kind kKind = Kind::FilterMode;
        svc::video::ScalerSlotMode mode{};
    };

    struct CoeffChunk {
        static constexpr Kind kKind = Kind::CoeffChunk;
        FrameWordCount count{};
        std::byte pad_{};
        std::array<hal::LinkWord, kCoeffChunkWords> w{};
    };

    struct ShadowMask {
        static constexpr Kind kKind = Kind::ShadowMask;
        svc::video::ShadowMaskWord flag{};
    };

    struct AudioTail {
        static constexpr Kind kKind = Kind::AudioTail;
        AfilterPushForm push = AfilterPushForm::Byte;
        proto::user_io::DeclaredSelect<proto::user_io::SelectRow::AfilterLead, svc::CoreVolume>
            lead{};
        proto::user_io::DeclaredSelect<proto::user_io::SelectRow::AudvolByFilter, svc::AudvolByte>
            audvol{};
        svc::FilterWordCount count{};
        std::array<hal::LinkWord, svc::kFilterWireWords - 1> words{};
    };

    struct ScalingPair {
        static constexpr Kind kKind = Kind::ScalingPair;
        svc::video::ScalingWords words{};
    };

    struct AudVol {
        static constexpr Kind kKind = Kind::AudVol;
        svc::AudvolByte vol{};
    };

    struct AfilterPush {
        static constexpr Kind kKind = Kind::AfilterPush;
        AfilterPushForm push = AfilterPushForm::Byte;
        proto::user_io::DeclaredSelect<proto::user_io::SelectRow::AfilterLead, svc::CoreVolume>
            lead{};
        svc::FilterWordCount count{};
        std::array<hal::LinkWord, svc::kFilterWireWords - 1> words{};
    };

    using Alternatives = std::tuple<
        Mode,
        ModeInBlock,
        FbufProbes,
        FilterMode,
        CoeffChunk,
        ShadowMask,
        AudioTail,
        ScalingPair,
        AudVol,
        AfilterPush>;

    static constexpr std::size_t kStore = 132;

    Kind kind{};
    std::byte pad_[3]{};
    alignas(std::uint32_t) std::array<std::byte, kStore> store{};
};

static_assert(infra::MessageSum<StagedFrame> && infra::alternatives_are_total<StagedFrame>());
static_assert(sizeof(StagedFrame) == 136 && alignof(StagedFrame) == alignof(std::uint32_t));
static_assert(std::is_trivially_copyable_v<StagedFrame>);

struct StagedFrameFacts {
    StagedFrame::Kind kind;
    Window window;
};

inline constexpr auto kStagedFrameFacts = std::to_array<StagedFrameFacts>({
    {StagedFrame::Kind::Mode, Window::Framed},
    {StagedFrame::Kind::ModeInBlock, Window::FramedReady},
    {StagedFrame::Kind::FbufProbes, Window::FramedReady},
    {StagedFrame::Kind::FilterMode, Window::FramedReady},
    {StagedFrame::Kind::CoeffChunk, Window::FramedReady},
    {StagedFrame::Kind::ShadowMask, Window::FramedReady},
    {StagedFrame::Kind::AudioTail, Window::FramedReady},
    {StagedFrame::Kind::ScalingPair, Window::FramedReady},
    {StagedFrame::Kind::AudVol, Window::FramedReady},
    {StagedFrame::Kind::AfilterPush, Window::FramedReady},
});
static_assert(infra::rows_are_ordinal(kStagedFrameFacts));

}
