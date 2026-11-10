// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/boot_ladder.h"

#include "cores/staging_core.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <utility>
#include <variant>

#include "cores/file_tx.h"
#include "infra/path_parts.h"
#include "svc/file.h"

namespace mister::cores {

BootLadder::BootLadder(const CoreProfile& profile, const LadderContext& ctx)
    : profile_(profile),
      vfs_(ctx.vfs),
      host_(ctx.host),
      clock_(ctx.clock) {
    switch (ctx.kind()) {
        case LadderContext::ArmKind::CoreStart: {
            const auto& start = std::get<LadderContext::CoreStartArm>(ctx.arm);
            core_start_ = true;
            index0_taken_ = start.index0_taken;
            start_rows_ = profile.start_assets;
            break;
        }
        case LadderContext::ArmKind::Mount: {
            const auto& mount = std::get<LadderContext::MountArm>(ctx.arm);
            image_.assign(mount.image);
            image_dir_.assign(dir_of(mount.image));

            report_.same_game = !mount.image.empty()
                && !ctx.last_dir.empty()
                && mount.image.starts_with(ctx.last_dir);

            report_.bios_found = report_.same_game;
            break;
        }
        case LadderContext::ArmKind::Reset:
        case LadderContext::ArmKind::Restore:
            break;
        case LadderContext::ArmKind::kCount:
            break;
    }
}

Ex<bool> BootLadder::step() {
    if (done())
        return finish_acts_();
    ++report_.rungs;

    while (start_row_ < start_rows_.size()
           && !start_row_sends(start_rows_[start_row_], index0_taken_))
        ++start_row_;
    if (start_row_ < start_rows_.size())
        return step_start_row_();
    if (core_start_ && !steps_at_core_start())
        return finish_acts_();
    auto r = on_step();
    if (!r) {

        if (reset_held_())
            (void)release_reset_();
        return r;
    }
    if (!*r)
        return r;
    return finish_acts_();
}

Ex<bool> BootLadder::step_start_row_() {
    auto w = load_row(start_rows_[start_row_]);

    if (!w && w.error().code == Errc::would_block)
        return false;
    if (!w)
        return std::unexpected(w.error());
    if (*w != AssetWait::Pending)
        ++start_row_;
    return false;
}

bool BootLadder::finish_acts_() {
    if (cheats_ordered_)
        return true;
    const CheatLookup& cl = profile_.cheats;
    const bool owed = report_.disc_mounted
        && !image_.empty()
        && cl.reset_on_mount
        && (!report_.same_game || cl.reset_on_remount);
    if (owed) {

        if (const auto id = host_.intern_path(image_, proto::FileSize{})) {
            if (!host_.order(
                    proto::LinkOp::StageCheats{.path = *id, .same_game = report_.same_game}
                )) {
                return false;
            }
        }
    }
    cheats_ordered_ = true;
    return true;
}

std::chrono::nanoseconds BootLadder::now_ns() const {
    return clock_.now().time_since_epoch();
}

std::string_view BootLadder::dir_of(std::string_view path) {
    const std::optional<std::string_view> dir = infra::dirname(path);
    return dir ? *dir : path;
}

std::string_view BootLadder::dir_or_empty(std::string_view path) {
    return infra::dirname(path).value_or(std::string_view{});
}

std::string_view BootLadder::base_of(std::string_view path) {
    return infra::basename(path);
}

std::string_view BootLadder::ext_of(std::string_view path) {
    return infra::extension(path);
}

std::string BootLadder::strip_disc_word(std::string path, std::span<const std::string_view> words) {
    std::size_t hit = std::string::npos;
    std::size_t len = 0;
    for (std::string_view w : words) {
        if (w.empty())
            continue;
        for (int form = 0; form < 3 && hit == std::string::npos; ++form) {
            std::string cand(w);
            if (form == 1) {
                cand[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(cand[0])));
            } else if (form == 2) {
                for (char& c : cand)
                    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            }
            hit = path.find(cand);
        }
        if (hit != std::string::npos) {
            len = w.size();
            break;
        }
    }
    if (hit == std::string::npos)
        return path;

    auto p1 = static_cast<std::ptrdiff_t>(hit);
    std::size_t p2 = hit + len;
    if (p1 > 0 && path[static_cast<std::size_t>(--p1)] == '(')
        --p1;
    if (p1 > 0 && path[static_cast<std::size_t>(--p1)] == ' ')
        --p1;

    auto at = [&](std::size_t i) {
        return i < path.size() ? path[i] : '\0';
    };
    if (at(p2) == ' ')
        ++p2;
    const char c = at(p2);
    const bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
    if (!hex)
        return path;
    ++p2;
    if (at(p2) == ')')
        ++p2;
    ++p1;
    path.erase(static_cast<std::size_t>(p1), p2 - static_cast<std::size_t>(p1));
    return path;
}

std::optional<std::string> BootLadder::resolve_row(const BootAsset& row) const {
    std::string cand;
    switch (row.anchor) {
        case AssetAnchor::Search: {
            std::string rel;
            if (!row.subdir.empty()) {
                rel.assign(row.subdir);
                rel += '/';
            }
            rel.append(row.name);
            const svc::SearchPolicy policy{row.where, true};
            auto r = vfs_.resolve(rel, policy);
            if (!r)
                return std::nullopt;
            return *r;
        }
        case AssetAnchor::ImageDir:

            if (image_.empty())
                return std::nullopt;
            if (image_.find('/') == std::string::npos)
                return std::nullopt;
            cand.assign(dir_of(image_));
            cand += '/';
            cand.append(row.name);
            return cand;
        case AssetAnchor::ImageParentDir:

            if (image_dir_.find('/') == std::string::npos)
                return std::nullopt;
            cand.assign(dir_of(image_dir_));
            cand += '/';
            cand.append(row.name);
            return cand;
        case AssetAnchor::CoreHome:
            return std::nullopt;
    }
    return std::nullopt;
}

Ex<AssetWait> BootLadder::load_row(const BootAsset& row) {
    if (!pieces_) {
        if (load_path_.empty()) {
            auto p = resolve_row(row);
            if (!p)
                return AssetWait::Miss;
            (void)load_path_.assign(*p);
        }
        auto f = vfs_.open(load_path_.view(), svc::OpenMode::ReadWhole);
        const auto sz = f ? (*f)->size() : Ex<svc::FileSize>{std::unexpected(f.error())};

        if (!sz || !row.accepts(*sz)) {
            load_path_.clear();
            return AssetWait::Miss;
        }
        const WideIoIndex dest = row.dest_for(marker_seen_);
        auto p = PayloadPieces::of_file(
            std::move(*f),
            *sz,
            ext_of(load_path_.view()),
            {.dest = dest, .chunk = WireChunk::of(kFileTxChunkBytes), .progress = true}
        );
        if (!p)
            return std::unexpected(p.error());
        pieces_.emplace(std::move(*p));
    }
    return step_pieces_();
}

Ex<AssetWait> BootLadder::step_pieces_() {
    auto pass = pieces_->step(host_);
    if (!pass) {
        const proto::FileSize sent = pieces_->sent();
        pieces_.reset();
        load_path_.clear();

        if (sent.empty())
            return AssetWait::Miss;
        return std::unexpected(pass.error());
    }
    if (*pass == PayloadPieces::Pass::Waiting)
        return AssetWait::Pending;
    const proto::FileSize size = pieces_->size();
    pieces_.reset();
    load_path_.clear();
    report_.bytes_downloaded += size;
    ++report_.assets_loaded;
    return AssetWait::Ready;
}

bool BootLadder::group_gates(std::uint8_t group) const {
    for (const BootAsset& row : profile_.boot_assets) {
        if (row.group == group)
            return row.group_required;
    }
    return false;
}

Ex<std::string> BootLadder::save_root_() {
    std::string root = "saves/";
    root.append(profile_.name);
    if (auto r = vfs_.ensure_dir(root); !r)
        return std::unexpected(r.error());
    return root;
}

Ex<std::string> BootLadder::save_path_for(std::string_view image) {
    auto root = save_root_();
    if (!root)
        return std::unexpected(root.error());
    std::string path = *root;
    path += '/';
    path.append(infra::stem(image));
    path += ".sav";
    const std::optional<SaveMountPolicy>& save = profile_.staging.save;
    return strip_disc_word(
        std::move(path),
        save ? save->disc_words : std::span<const std::string_view>{}
    );
}

Ex<std::string> BootLadder::dir_save_path_for(std::string_view dir) {
    auto root = save_root_();
    if (!root)
        return std::unexpected(root.error());
    std::string path = *root;
    path += '/';
    path.append(base_of(dir));
    path += ".sav";
    return path;
}

Ex<void> BootLadder::resolve_save_() {
    if (!save_path_.empty())
        return {};
    auto sp = save_path_for(image_);
    if (!sp)
        return std::unexpected(sp.error());
    save_path_ = std::move(*sp);
    return {};
}

Ex<void> BootLadder::resolve_dir_save_(std::string_view dir) {
    if (!save_path_.empty())
        return {};
    auto sp = dir_save_path_for(dir);
    if (!sp)
        return std::unexpected(sp.error());
    save_path_ = std::move(*sp);
    return {};
}

Ex<bool> BootLadder::prepare_save_once_() {
    if (save_ready_)
        return true;
    if (auto r = resolve_save_(); !r)
        return std::unexpected(r.error());
    if (!prepare_save(save_path_))
        return false;
    save_ready_ = true;
    return true;
}

Ex<bool> BootLadder::mount_save_rung_() {
    if (auto r = resolve_save_(); !r)
        return std::unexpected(r.error());
    if (!prepare_save(save_path_))
        return false;
    if (!order_mount_save(save_path_, true))
        return false;
    report_.save_mounted = true;
    return true;
}

MountState BootLadder::mount_bounded(std::string_view path) {
    return mount_slot_bounded(profile_.staging.disc_slot.block_slot(), path);
}

MountState BootLadder::mount_slot_bounded(proto::SlotIndex slot, std::string_view path) {
    if (!mount_gen_) {
        proto::FileId id{};
        if (!path.empty()) {
            auto r = host_.intern_path(path, proto::FileSize{});
            if (!r)
                return MountState::Failed;
            id = *r;
        }
        const proto::ActKey g = next_gen();

        if (!host_.order(
                proto::LinkOp::StageMount{
                    .slot = slot,
                    .same_game = report_.same_game,
                    .path = id,
                    .act_gen = g
                }
            )) {
            return MountState::Pending;
        }
        mount_gen_ = g;
        mount_due_ns_ = now_ns() + kDiscMountBoundNs;
        return MountState::Pending;
    }
    const MountStatus s = host_.mount_status();
    if (s.generation == mount_gen_ && s.state != MountState::Pending) {
        mount_answer_ = s;
        mount_gen_ = {};
        mount_due_ns_ = {};
        return s.state;
    }

    if (now_ns() >= mount_due_ns_) {
        mount_gen_ = {};
        mount_due_ns_ = {};
        return MountState::Failed;
    }
    return MountState::Pending;
}

bool BootLadder::prepare_save(std::string_view path) {
    const std::optional<SaveMountPolicy>& save = profile_.staging.save;
    if (!save)
        return false;
    if (!save_gen_) {
        auto id = host_.intern_path(path, proto::FileSize{});
        if (!id)
            return false;
        const proto::ActKey g = next_gen();
        if (!host_.order(
                proto::LinkOp::AttachSave{.slot = save->slot.block_slot(), .path = *id, .act = g}
            )) {
            return false;
        }
        save_gen_ = g;
        return false;
    }
    const SaveExtent e = host_.save_extent();
    if (e.generation != save_gen_)
        return false;
    switch (e.answer) {
        case SaveAnswer::Pending:
            return false;
        case SaveAnswer::Declined:

            save_gen_ = {};
            return false;
        case SaveAnswer::Known:
            break;
    }
    save_size_ = e.size_bytes;
    save_exists_ = e.exists;
    save_gen_ = {};
    return true;
}

bool BootLadder::order_mount_save(std::string_view path, bool bracketed) {
    const std::optional<SaveMountPolicy>& save = profile_.staging.save;
    if (!save)
        return false;
    proto::LinkOp::BindSlot op{.slot = save->slot.block_slot(), .bracketed = bracketed};
    if (path.empty()) {

        op.bind = proto::LinkOp::SlotBind::Unmount;
        return host_.order(op);
    }

    const bool pre = save_size_ == proto::FileSize{} && (!save_exists_ || save->pre_on_empty);
    const proto::FileSize size = pre ? save->pre_bytes : save_size_;
    auto id = host_.intern_path(path, size);
    if (!id)
        return false;
    op.bind = proto::LinkOp::SlotBind::Mount;
    op.path = *id;
    return host_.order(op);
}

bool BootLadder::order_announce_disc(proto::FileSize size_bytes) {
    proto::LinkOp::BindSlot op{
        .slot = profile_.staging.disc_slot.block_slot(),
        .bind = proto::LinkOp::SlotBind::MountCd
    };
    if (size_bytes != proto::FileSize{}) {
        auto id = host_.intern_path(image_, size_bytes);
        if (!id)
            return false;
        op.path = *id;
    }
    return host_.order(op);
}

bool BootLadder::order_notify_mount(bool loaded) {
    return host_.order(
        proto::LinkOp::AnnounceMount{
            .slot = profile_.staging.disc_slot.block_slot(),
            .loaded = loaded
        }
    );
}

bool BootLadder::order_status_bit0(bool asserted) {
    return host_.order(
        proto::LinkOp::WriteStatus{
            .start = proto::StatusBit{0},
            .width = 1,
            .value = asserted ? 1u : 0u
        }
    );
}

bool BootLadder::hold_reset_() {
    if (bit0_asserted_)
        return true;
    if (!order_status_bit0(true))
        return false;
    bit0_asserted_ = true;
    reset_hold_ns_ = now_ns();
    return true;
}

bool BootLadder::release_reset_() {
    if (!order_status_bit0(false))
        return false;
    bit0_asserted_ = false;
    return true;
}

bool BootLadder::pulse_reset_(std::chrono::microseconds pulse) {
    if (!bit0_asserted_)
        return true;
    if (now_ns() - reset_hold_ns_ < pulse)
        return false;
    return release_reset_();
}

bool BootLadder::order_reset() {
    return host_.order(proto::LinkOp::StageReset{});
}

bool BootLadder::order_disc_payload(const proto::LinkOp::StageDiscPayload& facts) {
    return host_.order(facts);
}

bool BootLadder::order_empty_bracket(WideIoIndex dest) {
    return host_.order(proto::LinkOp::StagePayload{.dest = dest});
}

bool BootLadder::order_set_index(WideIoIndex dest) {
    return host_.order(proto::LinkOp::SetWideIndex{.dest = dest});
}

}
