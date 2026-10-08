// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <tuple>

#include "infra/error.h"
#include "infra/message_sum.h"
#include "infra/seat.h"
#include "infra/spsc_ring.h"
#include "proto/conf_switches.h"
#include "proto/mailbox_poll.h"
#include "proto/ps2_frame.h"
#include "proto/reset_edge.h"
#include "proto/rotation_dir.h"
#include "proto/save_ask.h"
#include "proto/types.h"
#include "proto/osd_target.h"
#include "proto/volume_cmd.h"

namespace mister::proto {

struct LinkOp {
    TASTY_SEAT_EXEMPT(component);
    enum class Kind : std::uint8_t {
        HoldReset,
        ReleaseReset,
        WriteStatus,
        OsdFlush,
        JoyEmit,
        SendRtc,
        BlockAnswer,
        BindDecoders,
        BindSlot,
        FileTx,
        BindFacts,
        BindConfig,
        BindUart,
        BindSlotConfig,
        BindMount,
        BindSlotPreviews,
        RebootNow,
        ApplyCore,
        AbortSwitch,
        BindIdentity,
        BindDoorbells,
        BindJoy,
        JoyRelease,
        SaveUpload,
        SaveAsk,
        Ps2Frame,
        DropCore,
        MakeCore,
        SessionUp,
        StageMount,
        StagePayload,
        StageReset,
        StageDiscPayload,
        StageCheats,
        SetOsdVisible,
        PulseOption,
        SetVideoMode,
        ApplyCheats,
        SetDip,
        SetCoreOption,
        SettleCoreOptions,
        SetVolume,
        AnnounceMount,
        SetWideIndex,
        CoreReset,
        LoadFacts,
        MailboxWrite,
        kCount,
    };
    static constexpr std::size_t kStore = 12;

    enum class OsdShow : std::uint8_t { Off = 0, Menu = 1, Overlay = 2 };
    enum class SlotBind : std::uint8_t {
        Mount = 0,
        Unmount = 1,
        MountCd = 2,
        Attach = 3,
        Detach = 4
    };
    enum class DecoderTable : std::uint8_t { Census = 0, PreSession = 1 };

    struct HoldReset {
        static constexpr Kind kKind = Kind::HoldReset;
    };
    struct ReleaseReset {
        static constexpr Kind kKind = Kind::ReleaseReset;
    };
    struct WriteStatus {
        static constexpr Kind kKind = Kind::WriteStatus;
        StatusBit start{};
        std::uint8_t width = 0;
        std::uint8_t pad_[2]{};
        std::uint32_t value = 0;
    };
    struct OsdFlush {
        static constexpr Kind kKind = Kind::OsdFlush;
        OsdRow row{};
        std::uint8_t rows = 0;
        TxSlabId bytes{};
    };
    struct JoyEmit {
        static constexpr Kind kKind = Kind::JoyEmit;
        PlayerIndex player{};
        std::uint8_t pad_[3]{};
        JoyMask mask{};
        JoyMask autofire{};
    };
    struct SendRtc {
        static constexpr Kind kKind = Kind::SendRtc;
        std::uint32_t unix_seconds = 0;
    };
    struct BlockAnswer {
        static constexpr Kind kKind = Kind::BlockAnswer;
        SlotIndex slot{};
        std::uint8_t pad_{};
        std::uint16_t ack = 0;
        Lba lba{};
        std::uint32_t bytes = 0;
    };
    struct BindDecoders {
        static constexpr Kind kKind = Kind::BindDecoders;
        DecoderTable table{};
        std::uint8_t pad_{};
        BindGeneration gen{};
    };
    struct BindSlot {
        static constexpr Kind kKind = Kind::BindSlot;
        SlotIndex slot{};
        SlotBind bind{};
        bool bracketed = false;
        bool manual = false;
        FileId path{};
        std::uint8_t pad2_[2]{};
        std::uint32_t act_gen = 0;
    };

    enum class FileTxPhase : std::uint8_t {
        Whole = 0,
        Open = 1,
        Piece = 2,
        Close = 3,
        WindowOpen = 4,
        WindowClose = 5,
    };
    struct FileTx {
        static constexpr Kind kKind = Kind::FileTx;
        std::uint8_t wire_index = 0;
        FileTxPhase phase = FileTxPhase::Whole;
        FileId file{};
        std::uint32_t total = 0;
        std::uint32_t act = 0;
    };
    struct BindFacts {
        static constexpr Kind kKind = Kind::BindFacts;
        bool is_arcade = false;
        bool vertical = false;
        bool setname_same_dir = false;
        RotationDir rotation{};
        TxSlabId setname{};
    };
    struct BindConfig {
        static constexpr Kind kKind = Kind::BindConfig;
        bool savestate_dir_ready = false;
        bool ini_refused = false;
        bool waitmount_exhausted = false;
        std::uint8_t pad_{};
        TxSlabId cfg{};
        Errc parse_err{};
        ConfSwitches conf{};
    };
    struct BindUart {
        static constexpr Kind kKind = Kind::BindUart;
        bool probe_usb_ser = false;
        bool usb_ser = false;
        bool capable = false;
        std::uint8_t pad_{};
        TxSlabId mode{};
        TxSlabId speeds{};
        TxSlabId tokens{};
    };
    struct BindSlotConfig {
        static constexpr Kind kKind = Kind::BindSlotConfig;
        std::uint8_t slot = 0;
        std::uint8_t pad_{};
        FileId file{};
    };
    struct BindMount {
        static constexpr Kind kKind = Kind::BindMount;
        IoIndex index{};
        bool perform = false;
        FileId image{};
    };
    struct BindSlotPreviews {
        static constexpr Kind kKind = Kind::BindSlotPreviews;
        std::uint8_t count = 0;
    };
    struct RebootNow {
        static constexpr Kind kKind = Kind::RebootNow;
        bool cold = false;
        bool quiesce = false;
        std::uint16_t seq = 0;
    };
    struct ApplyCore {
        static constexpr Kind kKind = Kind::ApplyCore;
        bool loaded = false;
        std::uint8_t pad_{};
        BindGeneration gen{};
        CorrelationTag tag{};
    };
    struct AbortSwitch {
        static constexpr Kind kKind = Kind::AbortSwitch;
        Errc why{};
        BindGeneration gen{};
        CorrelationTag tag{};
    };
    struct BindIdentity {
        static constexpr Kind kKind = Kind::BindIdentity;
        TxSlabId blob{};
        bool declares_cheats = false;
        bool declares_turbo = false;
    };

    struct BindDoorbells {
        static constexpr Kind kKind = Kind::BindDoorbells;
        TxSlabId table{};
        std::uint8_t declared = 0;
        std::uint8_t rows = 0;
    };
    struct BindJoy {
        static constexpr Kind kKind = Kind::BindJoy;
        bool joyswap = false;
        bool suppress_analog_followup = false;
        std::uint8_t joy_transl = 0;
        std::uint8_t pad_{};
    };
    struct JoyRelease {
        static constexpr Kind kKind = Kind::JoyRelease;
        PlayerIndex player{};
        std::uint8_t pad_[3]{};
        JoyMask mask{};
    };
    struct SaveUpload {
        static constexpr Kind kKind = Kind::SaveUpload;
    };
    struct SaveAsk {
        static constexpr Kind kKind = Kind::SaveAsk;
        SaveKind which{};
        std::uint8_t slot = 0;
        std::uint8_t pad_[2]{};
        CorrelationTag tag{};
    };
    struct Ps2Frame {
        static constexpr Kind kKind = Kind::Ps2Frame;
        ::mister::proto::Ps2Frame frame{};
    };
    struct DropCore {
        static constexpr Kind kKind = Kind::DropCore;
    };
    struct MakeCore {
        static constexpr Kind kKind = Kind::MakeCore;
        bool manifest_hint = false;
        std::uint8_t pad_{};
        TxSlabId manifest{};
    };
    struct SessionUp {
        static constexpr Kind kKind = Kind::SessionUp;
    };
    struct StageMount {
        static constexpr Kind kKind = Kind::StageMount;
        SlotIndex slot{};
        bool same_game = false;
        FileId path{};
        std::uint32_t act_gen = 0;
    };

    enum class StagePhase : std::uint8_t { Whole = 0, Open = 1, Piece = 2, Close = 3 };
    struct StagePayload {
        static constexpr Kind kKind = Kind::StagePayload;
        FileId payload{};
        WideIoIndex dest{};
        static constexpr std::uint8_t kNoRegion = 0xFF;
        std::uint8_t region = kNoRegion;
        bool progress = false;
        StagePhase phase = StagePhase::Whole;

        std::uint8_t copy_word = 0;
        std::uint16_t chunk = 0;
        std::uint16_t act = 0;
    };
    struct StageReset {
        static constexpr Kind kKind = Kind::StageReset;
    };
    struct StageDiscPayload {
        static constexpr Kind kKind = Kind::StageDiscPayload;
        bool reset = false;
        std::uint8_t pad_{};
        std::uint16_t libcrypt_mask = 0;
    };
    struct StageCheats {
        static constexpr Kind kKind = Kind::StageCheats;
        FileId path{};
        bool same_game = false;
        std::uint8_t pad_{};
    };
    struct SetOsdVisible {
        static constexpr Kind kKind = Kind::SetOsdVisible;
        OsdShow show{};
        OsdTarget target = OsdTarget::All;
    };
    struct PulseOption {
        static constexpr Kind kKind = Kind::PulseOption;
        StatusBit bit{};
    };
    struct SetVideoMode {
        static constexpr Kind kKind = Kind::SetVideoMode;
        std::uint8_t block = 0;
        std::uint8_t pad_[3]{};
        std::uint32_t gen = 0;
    };
    struct ApplyCheats {
        static constexpr Kind kKind = Kind::ApplyCheats;
    };
    struct SetDip {
        static constexpr Kind kKind = Kind::SetDip;
        std::uint8_t row = 0;
        std::uint8_t pad_[3]{};
        std::uint32_t choice = 0;
    };
    struct SetCoreOption {
        static constexpr Kind kKind = Kind::SetCoreOption;
        std::uint8_t row = 0;
        std::uint8_t choice = 0;
    };
    struct SettleCoreOptions {
        static constexpr Kind kKind = Kind::SettleCoreOptions;
    };
    struct SetVolume {
        static constexpr Kind kKind = Kind::SetVolume;
        VolumeCmd cmd = VolumeCmd::Relative;
        std::int8_t arg = 0;
    };
    struct AnnounceMount {
        static constexpr Kind kKind = Kind::AnnounceMount;
        SlotIndex slot{};
        bool loaded = false;
    };
    struct SetWideIndex {
        static constexpr Kind kKind = Kind::SetWideIndex;
        WideIoIndex dest{};
    };

    struct CoreReset {
        static constexpr Kind kKind = Kind::CoreReset;
        ResetEdge edge{};
    };

    struct LoadFacts {
        static constexpr Kind kKind = Kind::LoadFacts;
        TxSlabId facts{};
    };

    struct MailboxWrite {
        static constexpr Kind kKind = Kind::MailboxWrite;
        std::uint8_t opcode = 0;
        MailboxPoll poll = MailboxPoll::Keep;
        std::uint16_t gen = 0;
        std::array<std::uint16_t, 3> words{};
    };

    using Alternatives =
        std::tuple<HoldReset, ReleaseReset, WriteStatus, OsdFlush, JoyEmit, SendRtc, BlockAnswer,
                   BindDecoders, BindSlot, FileTx, BindFacts, BindConfig, BindUart, BindSlotConfig,
                   BindMount, BindSlotPreviews, RebootNow, ApplyCore, AbortSwitch, BindIdentity,
                   BindDoorbells, BindJoy, JoyRelease, SaveUpload, SaveAsk, Ps2Frame, DropCore,
                   MakeCore, SessionUp, StageMount, StagePayload, StageReset, StageDiscPayload,
                   StageCheats, SetOsdVisible, PulseOption, SetVideoMode, ApplyCheats, SetDip,
                   SetCoreOption, SettleCoreOptions, SetVolume, AnnounceMount, SetWideIndex,
                   CoreReset, LoadFacts, MailboxWrite>;
    Kind kind = Kind::HoldReset;
    std::uint8_t pad_[3]{};
    alignas(4) std::array<std::byte, kStore> store{};
};

static_assert(infra::MessageSum<LinkOp> && infra::alternatives_are_total<LinkOp>());
static_assert(sizeof(LinkOp) == 16 && alignof(LinkOp) == 4);
static_assert(infra::ordinal(LinkOp::Kind::kCount) == 47);

[[nodiscard]] constexpr bool sets_region(const LinkOp::StagePayload& a) noexcept {
    return a.region != LinkOp::StagePayload::kNoRegion;
}

inline constexpr std::size_t kLinkTxCapacity = 64;
using LinkTxRing = xthread::SpscRing<LinkOp, kLinkTxCapacity>;

}  // namespace mister::proto
