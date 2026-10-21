// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/st_media_ladder.h"

#include <algorithm>
#include <utility>

#include "cores/st_config.h"
#include "cores/st_core.h"
#include "cores/staging_core.h"
#include "svc/file.h"

namespace mister::cores {

namespace {

constexpr std::uint8_t kTos = StCore::kTosSlot.v;
constexpr std::uint8_t kCart = StCore::kCartSlot.v;
constexpr std::uint8_t kAcsi0 = StCore::kAcsiSlot0.v;
static_assert(kCart < kLadderCarrySlots && StCore::kAcsiSlot1.v == kAcsi0 + 1);

}

StMediaLadder::StMediaLadder(const CoreProfile& profile, const LadderContext& ctx)
    : BootLadder(profile, ctx) {
    const std::size_t n = std::min(ctx.slot_paths.size(), paths_.size());
    for (std::size_t i = 0; i < n; ++i)
        paths_[i] = ctx.slot_paths[i];
    if (ctx.reset) {

        cold_ = ctx.reset->cold;
        const bool ejects = ctx.reset->source == proto::ResetSource::OptionAction && cold_;
        rung_ = ejects ? Rung::EjectA : Rung::Hold;
    } else if (ctx.restore) {
        cold_ = true;
        rung_ = Rung::Restore;
    } else {
        slot_ = ctx.slot.v;
        if (slot_ < paths_.size())
            paths_[slot_].assign(ctx.image_path);
        cold_ = slot_ == kTos;
        rung_ = Rung::Bind;
    }
}

Ex<bool> StMediaLadder::on_step() {
    TASTY_SEAT_BODY(StMediaLadder);
    switch (rung_) {
        case Rung::Bind:
            if (slot_ == kAcsi0 || slot_ == kAcsi0 + 1) {
                return bind_acsi_(slot_, paths_[slot_], Rung::Done);
            }
            return order_mount_(slot_, paths_[slot_], cold_ ? Rung::Hold : Rung::Done);
        case Rung::Restore:
            restore_();
            rung_ = Rung::Hold;
            return false;
        case Rung::EjectA:
            return order_mount_(0, {}, Rung::EjectB);
        case Rung::EjectB:
            return order_mount_(1, {}, Rung::Hold);
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
                if (auto r = intern_(kTos, std::move(bytes)); !r || !*r)
                    return r;
            }
            return order_file_(kTos, Rung::TosAnswer);
        case Rung::TosAnswer:
            return await_file_(Rung::Acsi0);
        case Rung::Acsi0:
        case Rung::Acsi1: {
            const bool first = rung_ == Rung::Acsi0;
            const auto slot = static_cast<std::uint8_t>(kAcsi0 + (first ? 0 : 1));
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
                if (auto r = intern_(kCart, std::move(bytes)); !r || !*r)
                    return r;
            }
            return order_file_(kCart, Rung::CartAnswer);
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
    std::vector<std::uint8_t> blob;
    if (!image_.empty()) {
        if (auto f = vfs_.open(image_, svc::OpenMode::ReadWhole)) {

            if (auto sz = (*f)->size(); sz && sz->v != 0) {
                blob.resize(
                    static_cast<std::size_t>(std::min<std::uint64_t>(sz->v, st_cfg::kBytes))
                );
                std::size_t off = 0;
                while (off < blob.size()) {
                    auto n = (*f)->read_at(
                        off,
                        std::as_writable_bytes(std::span<std::uint8_t>(blob).subspan(off))
                    );
                    if (!n || *n == 0) {
                        blob.clear();
                        break;
                    }
                    off += *n;
                }
            }
        }
    }
    StConfig c = StConfig::defaults();
    std::array<std::byte, st_cfg::kBytes> img{};
    c.overlay(std::as_bytes(std::span<const std::uint8_t>(blob)), paths_[kCart], img);
    paths_[kTos].assign(c.tos_img.view());
    for (std::size_t t = 0; t < StConfig::kAcsiTargets; ++t)
        paths_[kAcsi0 + t].assign(c.acsi_img[t].view());
    if (const auto cart = StConfig::cart_of(img); !cart.empty())
        paths_[kCart].assign(cart);
}

bool StMediaLadder::order_fact_(StMediaFact::Verb verb, Rung next) {
    const StMediaFact f{.verb = verb, .cold = cold_};
    const auto b = f.bytes();
    if (!host_.order_load_facts(b))
        return false;
    rung_ = next;
    return true;
}

Ex<bool> StMediaLadder::order_mount_(std::uint8_t slot, std::string_view path, Rung next) {
    proto::LinkOp::BindMount op{.index = proto::IoIndex{slot}, .perform = true};
    if (!path.empty()) {
        std::uint64_t size = 0;
        if (auto f = vfs_.open(path, svc::OpenMode::ReadWrite)) {
            if (auto sz = (*f)->size())
                size = sz->v;
        }
        auto id = host_.intern_path(path, size);
        if (!id) {
            if (id.error().code == Errc::would_block)
                return false;
            return std::unexpected(id.error());
        }
        if (*id == proto::FileId{})
            return std::unexpected(Error{Errc::io, ERR_SITE(), slot});
        op.image = *id;
    }
    if (!host_.order(op))
        return false;
    rung_ = next;
    return rung_ == Rung::Done;
}

Ex<bool> StMediaLadder::bind_acsi_(std::uint8_t slot, std::string_view path, Rung next) {
    if (mount_slot_bounded(proto::SlotIndex{slot}, path) == MountState::Pending)
        return false;
    rung_ = next;
    return rung_ == Rung::Done;
}

std::vector<std::uint8_t> StMediaLadder::read_tos_() const {
    const std::string& path = paths_[kTos];
    if (path.empty())
        return {};
    auto f = vfs_.open(path, svc::OpenMode::Read);
    if (!f)
        return {};
    return read_whole(**f, kTosReadCap);
}

std::vector<std::uint8_t> StMediaLadder::read_whole(svc::IFile& f, std::uint64_t cap) {
    std::vector<std::uint8_t> out;
    auto sz = f.size();
    if (!sz || sz->v > cap)
        return out;
    out.resize(static_cast<std::size_t>(sz->v));
    std::size_t off = 0;
    while (off < out.size()) {
        auto n = f.read_at(off, std::as_writable_bytes(std::span<std::uint8_t>(out).subspan(off)));
        if (!n || *n == 0) {
            out.clear();
            break;
        }
        off += *n;
    }
    return out;
}

std::vector<std::uint8_t> StMediaLadder::read_cart_() const {
    std::vector<std::uint8_t> out;
    const std::string& path = paths_[kCart];
    if (path.empty())
        return out;
    auto f = vfs_.open(path, svc::OpenMode::Read);
    if (!f)
        return out;
    out.assign(StCore::kCartBufferBytes, std::uint8_t{0xFF});
    std::size_t off = 0;
    while (off < out.size()) {
        auto n = (*f)->read_at(
            off,
            std::as_writable_bytes(std::span<std::uint8_t>(out).subspan(off))
        );
        if (!n || *n == 0)
            break;
        off += *n;
    }
    return out;
}

Ex<bool> StMediaLadder::intern_(std::uint8_t index, std::vector<std::uint8_t>&& bytes) {
    auto id = host_.intern_whole(std::move(bytes), paths_[index]);
    if (!id) {
        if (id.error().code == Errc::would_block)
            return false;
        return std::unexpected(id.error());
    }
    file_ = *id;
    return true;
}

Ex<bool> StMediaLadder::order_file_(std::uint8_t index, Rung answer) {
    const std::uint32_t act = next_gen();
    if (!host_.order(
            proto::LinkOp::FileTx{
                .wire_index = index,
                .phase = proto::LinkOp::FileTxPhase::Whole,
                .file = file_,
                .act = act
            }
        ))
        return false;
    file_ = proto::FileId{};
    act_ = act;
    due_ns_ = now_ns() + kDiscMountBoundNs;
    rung_ = answer;
    return false;
}

Ex<bool> StMediaLadder::await_file_(Rung next) {
    const auto served = host_.file_tx_answer(act_);
    if (!served) {
        if (now_ns() >= due_ns_)
            return std::unexpected(Error{Errc::timeout, ERR_SITE(), act_});
        return false;
    }
    if (!*served)
        return std::unexpected(Error{Errc::io, ERR_SITE(), act_});
    rung_ = next;
    return false;
}

}
