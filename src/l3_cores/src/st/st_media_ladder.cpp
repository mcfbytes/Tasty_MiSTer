// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/st/st_media_ladder.h"
#include "infra/span_cut.h"

#include <algorithm>
#include <array>
#include <span>
#include <utility>
#include <variant>

#include "cores/st/st_config.h"
#include "cores/st/st_core.h"
#include "cores/st/st_ctrl.h"
#include "cores/st/st_loader.h"
#include "cores/st/st_options.h"
#include "cores/staging_core.h"
#include "infra/byte_view.h"
#include "infra/path_parts.h"
#include "infra/prelude.h"
#include "svc/file.h"
#include "svc/read_bounded.h"

namespace mister::cores::st {

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
            if (!order_fact_(StMediaFact::Verb::Hold, cold_ ? Rung::Clear : Rung::Cart)) {
                return false;
            }
            return cold_ ? start_clear_() : false;
        case Rung::Clear: {
            if (!clear_) {
                rung_ = Rung::Tos;
                return false;
            }
            auto r = clear_->step(host_);
            if (!r)
                return std::unexpected(r.error());
            if (*r != PayloadPieces::Pass::Done)
                return false;

            if (host_.link_in_flight() != 0)
                return false;
            clear_.reset();
            rung_ = Rung::Tos;
            return false;
        }
        case Rung::Tos:
            return tos_rung_();
        case Rung::TosAnswer:
            return await_run_(Rung::Acsi0);
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
            return cart_rung_();
        case Rung::CartAnswer:
            return await_run_(Rung::Release);
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
    restored_dongle_ = c.system_ctrl.has(st_ctrl::CtrlWord::kDongle);
}

Ex<bool> StMediaLadder::start_clear_() {
    if (!clear_) {
        auto run = PayloadPieces::held_window(
            std::vector<std::uint8_t>(StCore::kClearBytes, 0),
            PayloadPieces::HeldFrame{
                .dest = proto::WideIoIndex::of(StCore::kClearIndex, 0),
                .framing = hal::Width::Word,
                .piece = StCore::kClearPieceWords
            }
        );
        if (!run)
            return std::unexpected(run.error());
        clear_ = std::move(*run);
    }

    auto r = clear_->step(host_);
    if (!r)
        return std::unexpected(r.error());
    return false;
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
        proto::FileSize size{};
        if (auto f = card_.open(path, svc::OpenMode::ReadWrite)) {
            if (auto sz = (*f)->size())
                size = *sz;
        }
        auto id = host_.intern_path(path, size);
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

    if (!read || proto::FileSize::of(std::span<const std::byte>(*read)) != *sz)
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
    out = StLoader::kCartBufferBytes.zeroed<std::vector<std::uint8_t>>();
    std::ranges::fill(out, std::to_integer<std::uint8_t>(StLoader::kBlankFill));

    [[maybe_unused]] const std::span<std::byte> written = svc::read_bounded_partial(
        **f,
        infra::byte_cast<std::byte>(std::span(out))
    );
    return out;
}

Ex<bool> StMediaLadder::tos_rung_() {
    if (!pieces_) {
        std::vector<std::uint8_t> bytes = read_tos_();

        auto plan = bytes.empty()
            ? Ex<LoadPlan>{LoadPlan{}}
            : plan_media_(st_ctrl::StSlot::Tos, proto::FileSize::of(std::span(bytes)));
        if (!plan)
            return std::unexpected(plan.error());
        if (plan->count == 0) {
            (void)order_fact_(StMediaFact::Verb::NoTos, Rung::Acsi0);
            return false;
        }
        if (auto r = start_run_(plan->rows[0], bytes, false); !r)
            return r;
    }
    return step_run_(Rung::TosAnswer);
}

Ex<bool> StMediaLadder::cart_rung_() {
    if (!pieces_) {
        dongle_ = dongle_now_();
        std::vector<std::uint8_t> bytes = dongle_ ? std::vector<std::uint8_t>{} : read_cart_();
        auto plan = plan_media_(st_ctrl::StSlot::Cart, proto::FileSize::of(std::span(bytes)));
        if (!plan)
            return std::unexpected(plan.error());
        if (plan->count == 0) {
            rung_ = Rung::Release;
            return false;
        }
        if (auto r = start_run_(plan->rows[0], bytes, true); !r)
            return r;
    }
    return step_run_(Rung::CartAnswer);
}

bool StMediaLadder::dongle_now_() {
    if (restored_dongle_)
        return *restored_dongle_;
    const auto choice = host_.option_choice(tag_of(StOption::Dongle));
    return choice.has_value() && *choice != proto::FieldValue{};
}

Ex<LoadPlan> StMediaLadder::plan_media_(st_ctrl::StSlot slot, proto::FileSize size) {
    TASTY_SEAT_BODY(StMediaLadder);
    StLoader loader;
    const LoadAsk ask{
        .index = proto::WideIoIndex::of(slot.io()),
        .path = paths_[slot],
        .size = size,
        .dongle = dongle_
    };
    return loader.plan(ask, host_.status_word());
}

Ex<bool> StMediaLadder::start_run_(
    const TransferRow& row,
    std::span<const std::uint8_t> bytes,
    bool held
) {
    std::vector<std::uint8_t> payload;
    if (row.file_len.empty()) {
        payload = row.extent.zeroed<std::vector<std::uint8_t>>();
        std::ranges::fill(std::as_writable_bytes(std::span{payload}), row.fill);
    } else {
        const proto::FileSize skip = row.file_offset - proto::kFileStart;
        const std::span<const std::uint8_t> part = infra::affine::first(
            infra::affine::from(bytes, skip),
            row.file_len
        );
        payload.assign(part.begin(), part.end());
    }
    const proto::ActKey16 act = next_gen().narrow();
    Ex<PayloadPieces> run = held ? PayloadPieces::held_window(
                                       std::move(payload),
                                       PayloadPieces::HeldFrame{
                                           .dest = row.index,
                                           .framing = host_.link_width(),
                                           .piece = StLoader::kCartPieceWords
                                       }
                                   )
                                 : PayloadPieces::of_bytes(
                                       std::move(payload),
                                       infra::extension(row.source),
                                       PayloadPieces::Frame{
                                           .dest = row.index,
                                           .chunk = StLoader::kTosChunk,
                                           .progress = true,
                                           .act = act
                                       }
                                   );
    if (!run)
        return std::unexpected(run.error());
    pieces_ = std::move(*run);
    act_ = held ? proto::ActKey16{} : act;
    return true;
}

Ex<bool> StMediaLadder::step_run_(Rung next) {
    auto r = pieces_->step(host_);
    if (!r)
        return std::unexpected(r.error());
    if (*r != PayloadPieces::Pass::Done)
        return false;
    due_ = os::Deadline::at_point(now() + kDiscMountBoundNs);
    rung_ = next;
    return false;
}

Ex<bool> StMediaLadder::await_run_(Rung next) {
    if (act_) {
        switch (host_.payload_answer(act_)) {
            case FileTxAnswer::Pending:
                if (due_.expired_at(now()))
                    return std::unexpected(Error{Errc::timeout, ERR_SITE(), act_.v});
                return false;
            case FileTxAnswer::Served:
                break;
            case FileTxAnswer::Dropped:
                return std::unexpected(Error{Errc::io, ERR_SITE(), act_.v});
        }
        act_ = {};
    } else if (host_.link_in_flight() != 0) {
        if (due_.expired_at(now()))
            return std::unexpected(Error{Errc::timeout, ERR_SITE(), 0});
        return false;
    }
    pieces_.reset();
    rung_ = next;
    return false;
}

}
