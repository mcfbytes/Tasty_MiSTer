// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/minimig/minimig_ladder.h"

#include <algorithm>
#include <utility>
#include <variant>

#include "cores/minimig/io_frame_sink_op.h"
#include "cores/minimig/minimig_fact.h"
#include "cores/minimig/minimig_core.h"
#include "cores/minimig/minimig_ladder_hrtmon.h"
#include "cores/wire_chunk.h"
#include "hal/types.h"
#include "proto/link_op.h"
#include "svc/read_bounded.h"
#include "svc/search_policy.h"

namespace mister::cores::minimig {

namespace {

constexpr hal::LinkWord kResetUser{0x1};
constexpr hal::LinkWord kResetCpu{0x2};
constexpr hal::LinkWord kHaltCpu{0x4};
constexpr hal::LinkWord kRunning{};
constexpr hal::LinkWord kHeld = kHaltCpu.with(kResetCpu);
constexpr hal::LinkWord kHeldInReset = kHeld.with(kResetUser);

constexpr hal::LinkWord kBootVideo{0x0400};
constexpr hal::LinkWord kBootAudio{};
constexpr hal::LinkWord kBootMemory{0x5};

constexpr proto::WideIoIndex kFrameDest = proto::WideIoIndex::of(proto::IoIndex{});

constexpr std::chrono::seconds kAnswerBound{5};

constexpr std::string_view kLogoFile = "MINIMIG.ART";
constexpr std::string_view kBallFile = "MINIMIG.BAL";
constexpr std::string_view kCopperFile = "MINIMIG.COP";
constexpr std::string_view kKeyFile = "ROM.KEY";
constexpr std::string_view kHrtmonFile = "HRTMON.ROM";

constexpr proto::FileSize kOneMore{1};
constexpr proto::FileSize kConfigReadCap = proto::FileSize{mm_cfg::kImageBytes} + kOneMore;
constexpr proto::FileSize kHrtmonReadCap = hrtmon::kLargestRom + kOneMore;

static_assert(
    MinimigCore::kKickstartSlot.below<kLadderCarrySlots>()
        && MinimigCore::kExtRomSlot.below<kLadderCarrySlots>(),
    "both ROM slots ride the host's carry"
);

}

MinimigLadder::MinimigLadder(const CoreProfile& profile, const LadderContext& ctx)
    : BootLadder(profile, ctx) {
    const std::size_t n = std::min(ctx.slot_paths.size(), paths_.size());
    std::copy_n(ctx.slot_paths.begin(), n, paths_.begin());
    switch (ctx.kind()) {
        case LadderContext::ArmKind::Mount: {
            const auto& mount = std::get<LadderContext::MountArm>(ctx.arm);

            if (mount.slot == MinimigCore::kKickstartSlot
                || mount.slot == MinimigCore::kExtRomSlot) {
                slot_ = mount.slot;
                picked_.assign(mount.image);
                rung_ = Rung::Bind;
            } else if (mount.slot.below<MinimigCore::kFloppyDrives>()) {
                slot_ = mount.slot;
                picked_.assign(mount.image);
                rung_ = Rung::Floppy;
            }
            break;
        }
        case LadderContext::ArmKind::Reset:
            rung_ = Rung::Sample;
            starts_nic_ = true;
            break;
        case LadderContext::ArmKind::Restore:
            config_.assign(std::get<LadderContext::RestoreArm>(ctx.arm).config);

            boots_ = paths_[MinimigCore::kKickstartSlot].empty();
            starts_nic_ = boots_;
            reload_ = true;
            rung_ = Rung::Restore;
            break;
        case LadderContext::ArmKind::CoreStart:
            break;
        case LadderContext::ArmKind::kCount:
            break;
    }
}

Ex<bool> MinimigLadder::on_step() {
    TASTY_SEAT_BODY(MinimigLadder);

    auto drained = drain_();
    if (!drained)
        return std::unexpected(drained.error());
    if (!*drained)
        return false;
    switch (rung_) {
        case Rung::Bind:
            return bind_pick_();
        case Rung::Floppy:
            return bind_floppy_();
        case Rung::Sample:
            return sample_live_();
        case Rung::Restore:
            restore_();
            rung_ = boots_ ? Rung::Prologue : Rung::Report;
            return false;
        case Rung::Prologue:
            prologue_();
            return false;
        case Rung::Draw:
            draw_();
            rung_ = Rung::Credits;
            return false;
        case Rung::Credits:
            credits_();
            rung_ = Rung::Report;
            return false;
        case Rung::Report:
            announce_();
            rung_ = Rung::Machine;
            return false;
        case Rung::Machine:
            machine_();
            rung_ = Rung::Hrtmon;
            return false;
        case Rung::Hrtmon:
            hrtmon_();
            rung_ = reload_ ? Rung::Kickstart : Rung::Release;
            return false;
        case Rung::Kickstart:
            kickstart_();
            return false;
        case Rung::Release:

            if (starts_nic_)
                host_.reset_share();
            release_();
            rung_ = Rung::WriteBack;
            return false;
        case Rung::WriteBack:
            return write_back_();
        case Rung::Nic:
            return start_nic_();
        case Rung::Done:
            return true;
    }
    return true;
}

Ex<bool> MinimigLadder::drain_() {
    while (run_ || !frames_.empty()) {
        if (!run_) {
            FrameQueue::Run next = frames_.take();

            act_ = frames_.empty() ? next_gen().narrow() : proto::ActKey16{};
            auto run = PayloadPieces::of_bytes(
                std::move(next.bytes),
                {},
                PayloadPieces::Frame{.dest = kFrameDest, .chunk = next.frame, .act = act_}
            );
            if (!run)
                return std::unexpected(run.error());
            run_ = std::move(*run);
        }
        auto pass = run_->step(host_);
        if (!pass)
            return std::unexpected(pass.error());
        if (*pass != PayloadPieces::Pass::Done)
            return false;
        report_.bytes_downloaded += run_->size();
        run_.reset();
        if (act_)
            answer_due_ = os::Deadline::at_point(now() + kAnswerBound);
    }
    if (act_) {
        switch (host_.payload_answer(act_)) {
            case FileTxAnswer::Pending:
                if (answer_due_.expired_at(now()))
                    return std::unexpected(Error{Errc::timeout, ERR_SITE(), 0});
                return false;
            case FileTxAnswer::Served:
                break;
            case FileTxAnswer::Dropped:
                return std::unexpected(Error{Errc::io, ERR_SITE(), 0});
        }
        act_ = proto::ActKey16{};
    }

    if (hold_)
        held_until_ = os::Deadline::at_point(now() + *std::exchange(hold_, std::nullopt));
    if (held_until_) {
        if (!held_until_->expired_at(now()))
            return false;
        held_until_.reset();
    }
    return true;
}

Ex<bool> MinimigLadder::bind_pick_() {
    auto ordered = order_bind_mount(slot_, picked_);
    if (!ordered)
        return std::unexpected(ordered.error());
    if (!*ordered)
        return false;
    rung_ = Rung::Done;
    return true;
}

Ex<bool> MinimigLadder::bind_floppy_() {
    if (mount_slot_bounded(slot_.block_slot(), picked_) == MountState::Pending)
        return false;
    rung_ = Rung::Done;
    return true;
}

Ex<bool> MinimigLadder::sample_live_() {
    const std::span<const std::byte> live = host_.config_value(cfg_.image);
    if (live.size() != cfg_.image.size()) {
        return std::unexpected(Error{Errc::not_found, ERR_SITE(), 0});
    }

    reload_ = cfg_.kickstart() != paths_[MinimigCore::kKickstartSlot]
        || cfg_.ext_rom() != paths_[MinimigCore::kExtRomSlot];
    rung_ = Rung::Machine;
    return false;
}

void MinimigLadder::restore_() {
    const std::vector<std::byte> file = read_(config_, kConfigReadCap);
    defaulted_ = !MinimigConfig::accepts(file);
    cfg_ = MinimigConfig::from_file(file);
}

void MinimigLadder::prologue_() {
    frames_.reg(FrameOp::Reset, kHeldInReset);
    frames_.reg(FrameOp::Reset, kHeld);
    if (!host_.boot_screen()) {
        rung_ = Rung::Report;
        return;
    }
    frames_.reg(FrameOp::Video, kBootVideo);
    frames_.reg(FrameOp::Audio, kBootAudio);
    hold_after_drain_(kVideoSettle);
    rung_ = Rung::Draw;
}

void MinimigLadder::draw_() {
    frames_.reg(FrameOp::Memory, kBootMemory);
    const std::vector<std::byte> logo = read_home_(kLogoFile, BootScreen::kLogoBytes);
    const std::vector<std::byte> ball = read_home_(kBallFile, BootScreen::kBallBytes);
    const std::vector<std::byte> copper = read_home_(kCopperFile, BootScreen::kCopperBytes);
    BootScreen::draw(frames_, BootScreen::Assets{.logo = logo, .ball = ball, .copper = copper});
    hold_after_drain_(kScreenHold);
}

void MinimigLadder::credits_() {
    screen_.print(frames_, "Minimig-AGA by Rok Krajnc. MiSTer port by Alexey Melnikov.");
    screen_.print(frames_, " ");
    screen_.print(frames_, "Original Minimig by Dennis van Weeren");
    screen_.print(
        frames_,
        "Updates by Jakub Bednarski, Tobias Gubener, Sascha Boing, "
        "A.M. Robinson & others"
    );
    screen_.print(frames_, " ");
}

void MinimigLadder::announce_() {
    if (defaulted_)
        screen_.print(frames_, ">>> No config found. Using defaults. <<<");
    count_refusal_(detach_floppies_());
    screen_.print(frames_, cfg_.summary_line().view());
}

void MinimigLadder::machine_() {
    frames_.reg(FrameOp::Cpu, cfg_.cpu_word());
    if (!reload_) {
        frames_.reg(FrameOp::Chipset, cfg_.chipset_word());
        frames_.reg(FrameOp::Floppy, cfg_.floppy_word());
    }
    count_refusal_(reset_hard_disks_());
    frames_.reg(FrameOp::Reset, kHaltCpu);
    const Ex<MinimigConfig::IdeUnits> opened = open_hard_disks_();
    count_refusal_(opened);
    frames_.reg(FrameOp::Hdd, cfg_.hdd_word(opened.value_or(MinimigConfig::IdeUnits{})));
    count_refusal_(open_cd_drives_());
    frames_.reg(FrameOp::Memory, cfg_.memory_word());
    frames_.reg(FrameOp::Cpu, cfg_.cpu_word());
    frames_.reg(FrameOp::Chipset, cfg_.chipset_word());
    frames_.reg(FrameOp::Floppy, cfg_.floppy_word());
}

void MinimigLadder::hrtmon_() {
    if (!cfg_.hrtmon())
        return;
    const std::vector<std::byte> rom = read_home_(kHrtmonFile, kHrtmonReadCap);
    const proto::FileSize size = proto::FileSize::of(std::span<const std::byte>(rom));
    if (size.empty() || size > hrtmon::kLargestRom)
        return;
    frames_.mem_blocks(kHrtmonRom, rom);
    frames_.mem(kHrtmonRom + hrtmon::kConfigAt, hrtmon::config_block(cfg_));
    frames_.mem(kHrtmonRom + hrtmon::kMaxChipAt, hrtmon::max_chip(cfg_));
}

void MinimigLadder::kickstart_() {
    frames_.reg(FrameOp::Reset, kHeld);
    const std::string configured{cfg_.kickstart()};
    bool uploaded = !cfg_.ext_rom().empty() && upload_with_ext_rom_();

    if (!uploaded)
        uploaded = upload_kickstart_(configured);
    if (!uploaded) {
        cfg_.set_kickstart(mm_cfg::kDefaultKickstart);
        uploaded = upload_kickstart_(cfg_.kickstart());
    }
    if (!uploaded) {
        cfg_.set_kickstart(mm_cfg::kKickstartName);
        uploaded = upload_kickstart_(cfg_.kickstart());
    }
    renamed_ = cfg_.kickstart() != configured;
    paths_[MinimigCore::kKickstartSlot].assign(cfg_.kickstart());
    paths_[MinimigCore::kExtRomSlot].assign(cfg_.ext_rom());
    if (!uploaded) {

        screen_.print(frames_, "No Kickstart loaded. Press F12 for settings.");
        screen_.print(frames_, "** Halted! **");
        halted_ = true;
        rung_ = Rung::WriteBack;
        return;
    }
    report_.bios_found = true;
    rung_ = Rung::Release;
}

void MinimigLadder::release_() {
    if (!reload_) {
        paths_[MinimigCore::kKickstartSlot].assign(cfg_.kickstart());
        paths_[MinimigCore::kExtRomSlot].assign(cfg_.ext_rom());
    }
    frames_.reg(FrameOp::Reset, kHeldInReset);
    frames_.reg(FrameOp::Reset, kRunning);
    frames_.reg(FrameOp::Video, cfg_.video_word());
    frames_.reg(FrameOp::Audio, cfg_.audio_word());
    frames_.reg(FrameOp::Joy, cfg_.joy_word());
    frames_.ext_cfg(cfg_.ext_cfg_halves());
}

Ex<bool> MinimigLadder::write_back_() {
    if (renamed_) {
        auto ordered = order_bind_mount(MinimigCore::kKickstartSlot, cfg_.kickstart());
        if (!ordered)
            return std::unexpected(ordered.error());
        if (!*ordered)
            return false;
    }
    if (starts_nic_) {
        rung_ = Rung::Nic;
        return false;
    }
    rung_ = Rung::Done;
    return true;
}

bool MinimigLadder::start_nic_() {
    if (!host_.start_nic(cfg_.nic_link()))
        return false;
    rung_ = Rung::Done;
    return true;
}

bool MinimigLadder::upload_kickstart_(std::string_view path) {
    const std::vector<std::byte> file = read_(path, Kickstart::kReadCap);
    if (file.empty())
        return false;
    const std::vector<std::byte> key = read_home_(kKeyFile, Kickstart::kKeyBuffer);

    frames_.discard_residents();
    const std::optional<Kickstart> rom = Kickstart::route(file, Kickstart::Key::usable(key));
    if (!rom)
        return false;
    queue_(*rom);
    return true;
}

bool MinimigLadder::upload_with_ext_rom_() {
    const std::vector<std::byte> ext = read_(cfg_.ext_rom(), Kickstart::kReadCap);
    const std::vector<std::byte> kick = read_(cfg_.kickstart(), Kickstart::kReadCap);
    const std::optional<Kickstart> rom = Kickstart::with_ext_rom({.kick = kick, .ext = ext});
    if (!rom)
        return false;
    frames_.discard_residents();
    queue_(*rom);
    return true;
}

void MinimigLadder::queue_(const Kickstart& rom) {
    for (const Kickstart::Region& region : rom.regions)
        frames_.mem_blocks(region.at, region.bytes);

    constexpr std::array<std::byte, 2> kNoTag{};
    if (rom.clears_tag)
        frames_.mem(kKickstartMirror, kNoTag);
    ++report_.assets_loaded;
}

Ex<void> MinimigLadder::detach_floppies_() {
    constexpr std::array kFacts{fact_byte(MinimigFact::DetachFloppies)};
    if (!host_.order_load_facts(kFacts))
        return std::unexpected(Error{Errc::would_block, ERR_SITE(), 0});
    return {};
}

Ex<void> MinimigLadder::reset_hard_disks_() {
    return unimplemented(ERR_SITE());
}

Ex<MinimigConfig::IdeUnits> MinimigLadder::open_hard_disks_() {
    return unimplemented(ERR_SITE());
}

Ex<void> MinimigLadder::open_cd_drives_() {
    return unimplemented(ERR_SITE());
}

std::vector<std::byte> MinimigLadder::read_(std::string_view path, proto::FileSize cap) const {
    if (path.empty())
        return {};
    auto bytes = svc::read_bounded(vfs_, path, cap.file_word<std::uint64_t>());
    if (!bytes)
        return {};
    return std::move(*bytes);
}

std::vector<std::byte> MinimigLadder::read_home_(std::string_view name, proto::FileSize cap) const {
    std::string rel{profile_.home_dir.empty() ? profile_.name : profile_.home_dir};
    rel += '/';
    rel += name;
    std::vector<std::byte> bytes;
    if (auto found = vfs_.resolve(rel, svc::SearchPolicy{kCoreHomeDir, true})) {
        bytes = read_(*found, cap);
    }
    if (bytes.empty())
        bytes = read_(name, cap);
    return bytes;
}

}
