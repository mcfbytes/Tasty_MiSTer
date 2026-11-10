// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/st_media_ladder.h"

#include <algorithm>
#include <array>
#include <span>
#include <utility>
#include <variant>

#include "cores/st_config.h"
#include "cores/st_core.h"
#include "cores/staging_core.h"
#include "infra/byte_view.h"
#include "svc/file.h"
#include "svc/read_bounded.h"

namespace mister::cores {

namespace {

static_assert(
    st_ctrl::StSlot::kCount <= kLadderCarrySlots,
    "the ST's slots must fit the host's carry"
);
static_assert(
    st_ctrl::kAcsiSlots.size() == StConfig::kAcsiTargets,
    "one ACSI slot per target the config names"
);

}

StMediaLadder::StMediaLadder(const CoreProfile& profile, const LadderContext& ctx)
    : BootLadder(profile, ctx) {
    const std::size_t n = std::min(ctx.slot_paths.size(), paths_.size());
    std::copy_n(ctx.slot_paths.begin(), n, paths_.begin());
    switch (ctx.kind()) {
        case LadderContext::ArmKind::Reset: {

            const auto& reset = std::get<LadderContext::ResetArm>(ctx.arm);
            cold_ = reset.edge.cold;
            const bool ejects = reset.edge.source == proto::ResetSource::OptionAction && cold_;
            rung_ = ejects ? Rung::EjectA : Rung::Hold;
            break;
        }
        case LadderContext::ArmKind::Restore:
            config_.assign(std::get<LadderContext::RestoreArm>(ctx.arm).config);
            cold_ = true;
            rung_ = Rung::Restore;
            break;
        case LadderContext::ArmKind::Mount: {
            const auto& mount = std::get<LadderContext::MountArm>(ctx.arm);
            if (const auto slot = st_ctrl::StSlot::of(mount.slot)) {
                slot_ = *slot;
                paths_[slot_].assign(mount.image);
                cold_ = slot_ == st_ctrl::StSlot::Tos;
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

Ex<bool> StMediaLadder::on_step() {
    TASTY_SEAT_BODY(StMediaLadder);
    switch (rung_) {
        case Rung::Bind:
            if (slot_.acsi_target())
                return bind_acsi_(slot_, paths_[slot_], Rung::Done);
            return order_mount_(slot_, paths_[slot_], cold_ ? Rung::Hold : Rung::Done);
        case Rung::Restore:
            restore_();
            rung_ = Rung::Hold;
            return false;
        case Rung::EjectA:
            return order_mount_(st_ctrl::StSlot::FloppyA, {}, Rung::EjectB);
        case Rung::EjectB:
            return order_mount_(st_ctrl::StSlot::FloppyB, {}, Rung::Hold);
        case Rung::Hold:
            (void)order_fact_(StMediaFact::Verb::Hold, cold_ ? Rung::Tos : Rung::Cart);
            return false;
        case Rung::Tos:
            if (file_ == proto::FileId{}) {
                auto bytes = read_tos_();
                if (bytes.empty()) {
                    (void)order_fact_(StMediaFact::Verb::NoTos, Rung::Acsi0);
                    return false;
                }
                if (auto r = intern_(st_ctrl::StSlot::Tos, std::move(bytes)); !r || !*r)
                    return r;
            }
            return order_file_(st_ctrl::StSlot::Tos, Rung::TosAnswer);
        case Rung::TosAnswer:
            return await_file_(Rung::Acsi0);
        case Rung::Acsi0:
        case Rung::Acsi1: {
            const bool first = rung_ == Rung::Acsi0;
            const st_ctrl::StSlot slot = first ? st_ctrl::StSlot::Acsi0 : st_ctrl::StSlot::Acsi1;
            const Rung next = first ? Rung::Acsi1 : Rung::Cart;

            const std::string& path = paths_[slot];
            if (!mount_outstanding() && (path.empty() || !vfs_.file_exists(path))) {
                rung_ = next;
                return false;
            }
            return bind_acsi_(slot, path, next);
        }
        case Rung::Cart:
            if (file_ == proto::FileId{}) {
                auto bytes = read_cart_();
                if (bytes.empty()) {
                    (void)order_fact_(StMediaFact::Verb::NoCart, Rung::Release);
                    return false;
                }
                if (auto r = intern_(st_ctrl::StSlot::Cart, std::move(bytes)); !r || !*r)
                    return r;
            }
            return order_file_(st_ctrl::StSlot::Cart, Rung::CartAnswer);
        case Rung::CartAnswer:
            return await_file_(Rung::Release);
        case Rung::Release:
            if (!order_fact_(StMediaFact::Verb::Release, Rung::Done))
                return false;
            return true;
        case Rung::Done:
            return true;
    }
    return true;
}

void StMediaLadder::restore_() {
    TASTY_SEAT_BODY(StMediaLadder);
    std::vector<std::byte> blob;
    if (!config_.empty()) {
        if (auto f = vfs_.open(config_, svc::OpenMode::ReadWhole)) {

            if (auto read = svc::read_bounded(**f, st_cfg::kBytes))
                blob = std::move(*read);
        }
    }
    StConfig c = StConfig::defaults();
    (void)c.cart_img.assign(paths_[st_ctrl::StSlot::Cart]);
    std::array<std::byte, st_cfg::kBytes> img{};
    c.overlay(blob, img);
    paths_[st_ctrl::StSlot::Tos].assign(c.tos_img.view());
    for (std::uint8_t t = 0; t < StConfig::kAcsiTargets; ++t)
        paths_[st_ctrl::kAcsiSlots[t]].assign(c.acsi_img[AcsiTarget{t}].view());
    if (!c.cart_img.empty())
        paths_[st_ctrl::StSlot::Cart].assign(c.cart_img.view());
}

bool StMediaLadder::order_fact_(StMediaFact::Verb verb, Rung next) {
    const StMediaFact f{.verb = verb, .cold = cold_};
    const auto b = f.bytes();
    if (!host_.order_load_facts(b))
        return false;
    rung_ = next;
    return true;
}

Ex<bool> StMediaLadder::order_mount_(st_ctrl::StSlot slot, std::string_view path, Rung next) {
    proto::LinkOp::BindMount op{.index = slot.io(), .perform = true};
    if (!path.empty()) {
        std::uint64_t size = 0;
        if (auto f = vfs_.open(path, svc::OpenMode::ReadWrite)) {
            if (auto sz = (*f)->size())
                size = sz->v;
        }
        auto id = host_.intern_path(path, proto::FileSize{size});
        if (!id) {
            if (id.error().code == Errc::would_block)
                return false;
            return std::unexpected(id.error());
        }
        op.image = *id;
    }
    if (!host_.order(op))
        return false;
    rung_ = next;
    return rung_ == Rung::Done;
}

Ex<bool> StMediaLadder::bind_acsi_(st_ctrl::StSlot slot, std::string_view path, Rung next) {
    if (mount_slot_bounded(slot.io().block_slot(), path) == MountState::Pending)
        return false;
    rung_ = next;
    return rung_ == Rung::Done;
}

std::vector<std::uint8_t> StMediaLadder::read_tos_() const {
    const std::string& path = paths_[st_ctrl::StSlot::Tos];
    if (path.empty())
        return {};
    auto f = vfs_.open(path, svc::OpenMode::Read);
    if (!f)
        return {};
    return read_whole(**f, kTosReadCap);
}

std::vector<std::uint8_t> StMediaLadder::read_whole(svc::IFile& f, std::uint64_t cap) {
    const auto sz = f.size();

    if (!sz || !sz->within(svc::FileSize{0u}, svc::FileSize{cap}))
        return {};
    auto read = svc::read_bounded(f, cap);

    if (!read || read->size() != sz->v)
        return {};
    const auto got = infra::byte_cast<const std::uint8_t>(std::span(*read));
    return std::vector<std::uint8_t>(got.begin(), got.end());
}

std::vector<std::uint8_t> StMediaLadder::read_cart_() const {
    std::vector<std::uint8_t> out;
    const std::string& path = paths_[st_ctrl::StSlot::Cart];
    if (path.empty())
        return out;
    auto f = vfs_.open(path, svc::OpenMode::Read);
    if (!f)
        return out;
    out.assign(StCore::kCartBufferBytes, std::uint8_t{0xFF});

    [[maybe_unused]] const std::span<std::byte> written = svc::read_bounded_partial(
        **f,
        infra::byte_cast<std::byte>(std::span(out))
    );
    return out;
}

Ex<bool> StMediaLadder::intern_(st_ctrl::StSlot slot, std::vector<std::uint8_t>&& bytes) {
    auto id = host_.intern_whole(std::move(bytes), paths_[slot]);
    if (!id) {
        if (id.error().code == Errc::would_block)
            return false;
        return std::unexpected(id.error());
    }
    file_ = *id;
    return true;
}

Ex<bool> StMediaLadder::order_file_(st_ctrl::StSlot slot, Rung answer) {
    const proto::ActKey act = next_gen();
    const proto::WideIoIndex index = proto::WideIoIndex::of(slot.io(), 0);
    const proto::LinkOp::FileTx op{
        .wire_index = index.fio_byte(),
        .phase = proto::LinkOp::FileTxPhase::Whole,
        .file = file_,
        .act = act
    };
    if (!host_.order(op))
        return false;
    file_ = proto::FileId{};
    act_ = act;
    due_ns_ = now_ns() + kDiscMountBoundNs;
    rung_ = answer;
    return false;
}

Ex<bool> StMediaLadder::await_file_(Rung next) {
    switch (host_.file_tx_answer(act_)) {
        case FileTxAnswer::Pending:
            if (now_ns() >= due_ns_)
                return std::unexpected(Error{Errc::timeout, ERR_SITE(), act_.v});
            return false;
        case FileTxAnswer::Served:
            rung_ = next;
            return false;
        case FileTxAnswer::Dropped:
            break;
    }
    return std::unexpected(Error{Errc::io, ERR_SITE(), act_.v});
}

}
