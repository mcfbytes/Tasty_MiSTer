// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/archie/archie_ladder.h"

#include <algorithm>
#include <ctime>
#include <utility>
#include <variant>

#include "cores/archie/archie_config.h"
#include "cores/cmos_image.h"
#include "infra/byte_view.h"
#include "infra/span_cut.h"
#include "svc/file.h"
#include "svc/read_bounded.h"

namespace mister::cores::archie {

namespace {

static_assert(
    ArchieCore::kRomSlot.below<kLadderCarrySlots>(),
    "the ROM slot's path rides the host's carry"
);

}

ArchieLadder::ArchieLadder(const CoreProfile& profile, const LadderContext& ctx)
    : BootLadder(profile, ctx) {
    const std::size_t n = std::min(ctx.slot_paths.size(), paths_.size());
    std::copy_n(ctx.slot_paths.begin(), n, paths_.begin());
    switch (ctx.kind()) {
        case LadderContext::ArmKind::Restore:
            config_.assign(std::get<LadderContext::RestoreArm>(ctx.arm).config);
            boots_ = true;
            rung_ = Rung::Restore;
            break;
        case LadderContext::ArmKind::Reset:

            rung_ = Rung::Rom;
            break;
        case LadderContext::ArmKind::Mount: {
            const auto& mount = std::get<LadderContext::MountArm>(ctx.arm);
            if (mount.slot == ArchieCore::kRomSlot && !mount.image.empty()) {
                rom_path_().assign(mount.image);
                rung_ = Rung::Bind;
            }
            break;
        }
        case LadderContext::ArmKind::CoreStart:
            break;
        case LadderContext::ArmKind::kCount:
            break;
    }
}

Ex<bool> ArchieLadder::on_step() {
    TASTY_SEAT_BODY(ArchieLadder);
    switch (rung_) {
        case Rung::Restore:
            restore_();
            rung_ = Rung::Hold;
            return false;
        case Rung::Hold:
            if (!hold_reset_())
                return false;
            rung_ = Rung::Bind;
            return false;
        case Rung::Bind:
            return bind_rom_();
        case Rung::Rom:
            return rom_rung_();
        case Rung::Cmos:
            return cmos_rung_();
        case Rung::Release:
            if (!release_reset_())
                return false;
            rung_ = Rung::Done;
            return true;
        case Rung::Done:
            return true;
    }
    return true;
}

void ArchieLadder::restore_() {
    if (config_.empty())
        return;
    auto f = vfs_.open(config_, svc::OpenMode::ReadWhole);
    if (!f)
        return;
    const auto size = (*f)->size();
    if (!size || *size != svc::FileSize{ArchieConfig::kBytes})
        return;
    const auto blob = svc::read_bounded(**f, ArchieConfig::kBytes);
    if (!blob || blob->size() != ArchieConfig::kBytes)
        return;
    const ArchieConfig c = ArchieConfig::decode(
        ArchieConfig::ConstImage{blob->data(), ArchieConfig::kBytes}
    );
    rom_path_().assign(c.rom_img.view());
}

Ex<bool> ArchieLadder::bind_rom_() {
    if (rom_path_().empty()) {
        if (const auto found = resolve_row(kDefaultRom))
            rom_path_() = *found;
    }
    if (!rom_path_().empty()) {
        const auto ordered = order_bind_mount(ArchieCore::kRomSlot, rom_path_());
        if (!ordered)
            return std::unexpected(ordered.error());
        if (!*ordered)
            return false;
    }
    rung_ = Rung::Rom;
    return false;
}

Ex<bool> ArchieLadder::rom_rung_() {
    if (!pieces_ && load_path_.empty() && !rom_path_().empty()) {
        (void)load_path_.assign(rom_path_());
    }
    const auto wait = load_row(kDefaultRom);
    if (!wait)
        return std::unexpected(wait.error());
    if (*wait == AssetWait::Pending)
        return false;
    rung_ = boots_ ? Rung::Cmos : Rung::Done;
    return rung_ == Rung::Done;
}

std::string ArchieLadder::cmos_path_() const {
    const bool saved = vfs_.file_exists(kCmosSaved);
    const auto stock = resolve_row(kCmosStock);
    if (!stock)
        return saved ? std::string{kCmosSaved} : std::string{};
    if (!saved)
        return *stock;
    const auto ours = vfs_.modified(kCmosSaved);
    const auto theirs = vfs_.modified(*stock);
    return ours && theirs && *theirs > *ours ? *stock : std::string{kCmosSaved};
}

std::vector<std::uint8_t> ArchieLadder::read_cmos_() const {
    const std::string path = cmos_path_();
    if (path.empty())
        return {};
    auto f = vfs_.open(path, svc::OpenMode::ReadWhole);
    if (!f)
        return {};
    const auto read = svc::read_bounded(**f, proto::FileSize::of(ArchieCore::kCmos.bytes));
    if (!read || read->empty())
        return {};

    std::vector<std::byte> image = proto::FileSize::of(ArchieCore::kCmos.bytes)
                                       .zeroed<std::vector<std::byte>>();
    const auto got = infra::affine::first_clamped(std::span{*read}, ArchieCore::kCmos.bytes);
    std::ranges::copy(got, image.begin());
    const std::time_t now = std::time(nullptr);
    std::tm local{};
    if (::localtime_r(&now, &local) == nullptr)
        return {};
    if (!patch_for_load(ArchieCore::kCmos, compose_rtc(local), image))
        return {};
    const auto bytes = infra::byte_cast<const std::uint8_t>(std::span(image));
    return std::vector<std::uint8_t>(bytes.begin(), bytes.end());
}

Ex<bool> ArchieLadder::cmos_rung_() {
    if (!cmos_ && !cmos_read_) {
        cmos_read_ = true;
        std::vector<std::uint8_t> image = read_cmos_();
        if (!image.empty()) {
            auto run = PayloadPieces::held_window(
                std::move(image),
                PayloadPieces::HeldFrame{
                    .dest = ArchieCore::kCmos.index,
                    .framing = host_.link_width(),
                    .piece = kCmosPieceWords
                }
            );
            if (!run)
                return std::unexpected(run.error());
            cmos_ = std::move(*run);
        }
    }
    if (cmos_) {
        const auto pass = cmos_->step(host_);
        if (!pass)
            return std::unexpected(pass.error());
        if (*pass != PayloadPieces::Pass::Done)
            return false;
        cmos_.reset();
    }
    rung_ = Rung::Release;
    return false;
}

}
