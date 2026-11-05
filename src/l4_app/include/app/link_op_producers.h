// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <string_view>
#include <tuple>
#include <type_traits>

#include "infra/message_sum.h"
#include "proto/link_op.h"

namespace mister::app {
class BindTail;
class BoardReboot;
class CmdRouter;
class CompanionWalk;
class ConfigSaves;
class ContentPick;
class CoreBuilder;
class CoreSwitch;
class FileTxPieces;
class InputDecode;
class JoyLevelEmitter;
class LadderHostAdapter;
class LinkSession;
class LoadLadder;
class LoadWalk;
class ManifestLadder;
class MglPlayer;
class MountQueue;
class SessionOwner;
class SettleStage;
class VideoWire;
}

namespace mister::cores {
class BootLadder;
class PayloadPieces;
class StMediaLadder;
}

namespace mister::ui {
class CheatsPage;
class CoreOptionsPage;
class CoreSettingsPage;
class DipsPage;
class UiRound;
}

namespace mister::fw {
class TastySession;
}

namespace mister::app {

template <proto::LinkOp::Kind K, class... P>
struct OpProducer {
    static constexpr proto::LinkOp::Kind kKind = K;
    static constexpr std::size_t kProducers = sizeof...(P);
};

template <proto::LinkOp::Kind K, const std::string_view& Why>
struct OpUnproduced {
    static constexpr proto::LinkOp::Kind kKind = K;
    static constexpr std::string_view kWhy = Why;
};

inline constexpr std::string_view kOwedOsdFlush = "no producer: OsdWire flushes the live row in "
                                                  "place; the arm is kept and tested";
inline constexpr std::string_view
    kOwedSendRtc = "no producer: no periodic RTC sender exists; the arm is kept and tested";
inline constexpr std::string_view kOwedBlockAnswer =
    "no producer: the block answer is a wire reply the storage seat issues; the arm is tested";

using LinkOpProducers = std::tuple<
    OpProducer<proto::LinkOp::Kind::HoldReset, CoreSwitch, SessionOwner>,
    OpProducer<proto::LinkOp::Kind::ReleaseReset, SessionOwner>,
    OpProducer<
        proto::LinkOp::Kind::WriteStatus,
        BindTail,
        LoadWalk,
        SettleStage,
        cores::BootLadder,
        ui::CoreOptionsPage>,
    OpUnproduced<proto::LinkOp::Kind::OsdFlush, kOwedOsdFlush>,
    OpProducer<proto::LinkOp::Kind::JoyEmit, JoyLevelEmitter>,
    OpUnproduced<proto::LinkOp::Kind::SendRtc, kOwedSendRtc>,
    OpUnproduced<proto::LinkOp::Kind::BlockAnswer, kOwedBlockAnswer>,
    OpProducer<proto::LinkOp::Kind::BindDecoders, CoreSwitch, SessionOwner>,
    OpProducer<proto::LinkOp::Kind::BindSlot, LoadLadder, LoadWalk, MountQueue, cores::BootLadder>,
    OpProducer<
        proto::LinkOp::Kind::FileTx,
        ContentPick,
        FileTxPieces,
        LoadLadder,
        cores::StMediaLadder>,
    OpProducer<proto::LinkOp::Kind::BindFacts, BindTail>,
    OpProducer<proto::LinkOp::Kind::BindConfig, SessionOwner>,
    OpProducer<proto::LinkOp::Kind::BindUart, BindTail>,
    OpProducer<proto::LinkOp::Kind::BindSlotConfig, ConfigSaves>,
    OpProducer<proto::LinkOp::Kind::BindMount, MountQueue, cores::StMediaLadder>,
    OpProducer<proto::LinkOp::Kind::BindSlotPreviews, ConfigSaves>,
    OpProducer<proto::LinkOp::Kind::RebootNow, BoardReboot>,
    OpProducer<proto::LinkOp::Kind::ApplyCore, CoreSwitch, SessionOwner>,
    OpProducer<proto::LinkOp::Kind::AbortSwitch, SessionOwner>,
    OpProducer<proto::LinkOp::Kind::BindIdentity, BindTail>,
    OpProducer<proto::LinkOp::Kind::BindDoorbells, BindTail>,
    OpProducer<proto::LinkOp::Kind::BindJoy, BindTail>,
    OpProducer<proto::LinkOp::Kind::JoyRelease, JoyLevelEmitter>,
    OpProducer<proto::LinkOp::Kind::SaveUpload, ConfigSaves>,
    OpProducer<proto::LinkOp::Kind::SaveAsk, ConfigSaves>,
    OpProducer<proto::LinkOp::Kind::Ps2Frame, InputDecode>,
    OpProducer<proto::LinkOp::Kind::DropCore, CoreSwitch>,
    OpProducer<proto::LinkOp::Kind::AdoptCore, CoreBuilder>,
    OpProducer<proto::LinkOp::Kind::SessionUp, SessionOwner>,
    OpProducer<proto::LinkOp::Kind::StageMount, cores::BootLadder>,
    OpProducer<
        proto::LinkOp::Kind::StagePayload,
        LoadLadder,
        LoadWalk,
        ManifestLadder,
        cores::BootLadder,
        cores::PayloadPieces>,
    OpProducer<proto::LinkOp::Kind::StageReset, cores::BootLadder>,
    OpProducer<proto::LinkOp::Kind::StageDiscPayload, cores::BootLadder>,
    OpProducer<proto::LinkOp::Kind::StageCheats, cores::BootLadder>,
    OpProducer<proto::LinkOp::Kind::SetOsdVisible, fw::TastySession, ui::UiRound>,
    OpProducer<proto::LinkOp::Kind::PulseOption, LinkSession, MglPlayer, ui::DipsPage>,
    OpProducer<proto::LinkOp::Kind::SetVideoMode, VideoWire>,
    OpProducer<proto::LinkOp::Kind::ApplyCheats, ui::CheatsPage>,
    OpProducer<proto::LinkOp::Kind::SetDip, ui::DipsPage>,
    OpProducer<proto::LinkOp::Kind::SetCoreOption, ui::CoreSettingsPage>,
    OpProducer<proto::LinkOp::Kind::SetVolume, CmdRouter>,
    OpProducer<proto::LinkOp::Kind::AnnounceMount, cores::BootLadder>,
    OpProducer<proto::LinkOp::Kind::SetWideIndex, cores::BootLadder>,
    OpProducer<proto::LinkOp::Kind::CoreReset, SessionOwner>,
    OpProducer<proto::LinkOp::Kind::LoadFacts, FileTxPieces, LadderHostAdapter, LoadLadder>,
    OpProducer<proto::LinkOp::Kind::MailboxWrite, CompanionWalk>>;

namespace detail {
template <class T>
struct IsOpUnproduced : std::false_type {};

template <proto::LinkOp::Kind K, const std::string_view& W>
struct IsOpUnproduced<OpUnproduced<K, W>> : std::true_type {};

template <class T>
struct OpEntry;

template <proto::LinkOp::Kind K, class... P>
struct OpEntry<OpProducer<K, P...>> {
    static constexpr proto::LinkOp::Kind kKind = K;
    static constexpr bool kWellFormed = sizeof...(P) > 0;
};

template <proto::LinkOp::Kind K, const std::string_view& W>
struct OpEntry<OpUnproduced<K, W>> {
    static constexpr proto::LinkOp::Kind kKind = K;
    static constexpr bool kWellFormed = true;
};
}

template <class List>
consteval bool producers_cover_each_op_kind() {
    std::array<unsigned, infra::ordinal(proto::LinkOp::Kind::kCount)> seen{};
    bool well_formed = true;
    [&]<class... E>(std::tuple<E...>*) {
        (
            [&] {
                constexpr auto e = detail::OpEntry<E>{};
                ++seen[infra::ordinal(e.kKind)];
                well_formed = well_formed && e.kWellFormed;
            }(),
            ...);
    }(static_cast<List*>(nullptr));
    for (unsigned n : seen)
        if (n != 1)
            return false;
    return well_formed;
}

static_assert(
    producers_cover_each_op_kind<LinkOpProducers>(),
    "every LinkOp kind is in exactly one OpProducer (with a producer) or one "
    "OpUnproduced entry"
);

}
