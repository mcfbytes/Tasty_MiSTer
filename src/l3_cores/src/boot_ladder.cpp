// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/boot_ladder.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <utility>

#include "svc/file.h"

namespace mister::cores {

BootLadder::BootLadder(const CoreProfile& profile, const LadderContext& ctx)
    : profile_(profile), vfs_(ctx.vfs), host_(ctx.host), clock_(ctx.clock),
      core_start_(ctx.at_core_start), index0_taken_(ctx.at_core_start && ctx.index0_taken) {
    if (ctx.at_core_start) start_rows_ = profile.start_assets;
    image_.assign(ctx.image_path);
    image_dir_.assign(dir_of(ctx.image_path));

    report_.same_game = !ctx.image_path.empty() && !ctx.last_dir.empty() &&
                        ctx.image_path.starts_with(ctx.last_dir);

    report_.bios_found = report_.same_game;
}

Ex<bool> BootLadder::step() {
    if (done()) return finish_acts_();
    ++report_.rungs;

    while (start_row_ < start_rows_.size() &&
           !start_row_sends(start_rows_[start_row_], index0_taken_))
        ++start_row_;
    if (start_row_ < start_rows_.size()) return step_start_row_();
    if (core_start_ && !steps_at_core_start()) return finish_acts_();
    auto r = on_step();
    if (!r || !*r) return r;
    return finish_acts_();
}

Ex<bool> BootLadder::step_start_row_() {
    auto w = load_row(start_rows_[start_row_]);

    if (!w && w.error().code == Errc::would_block) return false;
    if (!w) return std::unexpected(w.error());
    if (*w != AssetWait::Pending) ++start_row_;
    return false;
}

bool BootLadder::finish_acts_() {
    if (cheats_ordered_) return true;
    const CheatLookup& cl = profile_.cheats;
    const bool owed = report_.disc_mounted && !image_.empty() && cl.reset_on_mount &&
                      (!report_.same_game || cl.reset_on_remount);
    if (owed) {
        proto::FileId id{};
        if (auto r = host_.intern_path(image_, 0); r && *r != proto::FileId{}) id = *r;

        if (id != proto::FileId{}) {
            if (!host_.order(
                    proto::LinkOp::StageCheats{.path = id, .same_game = report_.same_game})) {
                return false;
            }
        }
    }
    cheats_ordered_ = true;
    return true;
}

std::int64_t BootLadder::now_ns() const { return clock_.now().count(); }

std::string_view BootLadder::dir_of(std::string_view path) {
    const auto slash = path.rfind('/');
    if (slash == std::string_view::npos) return path;
    return path.substr(0, slash);
}

std::string_view BootLadder::base_of(std::string_view path) {
    const auto slash = path.rfind('/');
    return slash == std::string_view::npos ? path : path.substr(slash + 1);
}

std::string_view BootLadder::ext_of(std::string_view path) {
    const std::string_view name = base_of(path);
    const auto dot = name.rfind('.');
    if (dot == std::string_view::npos) return {};
    return name.substr(dot);
}

std::string BootLadder::strip_disc_word(std::string path, std::span<const std::string_view> words) {
    std::size_t hit = std::string::npos;
    std::size_t len = 0;
    for (std::string_view w : words) {
        if (w.empty()) continue;
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
    if (hit == std::string::npos) return path;

    auto p1 = static_cast<std::ptrdiff_t>(hit);
    std::size_t p2 = hit + len;
    if (p1 > 0 && path[static_cast<std::size_t>(--p1)] == '(') --p1;
    if (p1 > 0 && path[static_cast<std::size_t>(--p1)] == ' ') --p1;

    auto at = [&](std::size_t i) { return i < path.size() ? path[i] : '\0'; };
    if (at(p2) == ' ') ++p2;
    const char c = at(p2);
    const bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
    if (!hex) return path;
    ++p2;
    if (at(p2) == ')') ++p2;
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
            if (!r) return std::nullopt;
            return *r;
        }
        case AssetAnchor::ImageDir:

            if (image_.empty()) return std::nullopt;
            if (image_.find('/') == std::string::npos) return std::nullopt;
            cand.assign(dir_of(image_));
            cand += '/';
            cand.append(row.name);
            return cand;
        case AssetAnchor::ImageParentDir:

            if (image_dir_.find('/') == std::string::npos) return std::nullopt;
            cand.assign(dir_of(image_dir_));
            cand += '/';
            cand.append(row.name);
            return cand;
    }
    return std::nullopt;
}

Ex<AssetWait> BootLadder::load_row(const BootAsset& row) {
    if (!pieces_) {
        if (load_path_.empty()) {
            auto p = resolve_row(row);
            if (!p) return AssetWait::Miss;
            (void)load_path_.assign(*p);
        }
        auto f = vfs_.open(load_path_.view(), svc::OpenMode::ReadWhole);
        const auto sz = f ? (*f)->size() : Ex<svc::FileSize>{std::unexpected(f.error())};

        if (!sz || (row.exact_size != 0 && sz->v != row.exact_size)) {
            load_path_.clear();
            return AssetWait::Miss;
        }
        const WideIoIndex dest =
            (marker_seen_ && row.dest_if_marker.v != 0) ? row.dest_if_marker : row.dest;
        auto p = PayloadPieces::of_file(std::move(*f), sz->v, ext_of(load_path_.view()),
                                        {.dest = dest, .chunk = kGenericChunk, .progress = true});
        if (!p) return std::unexpected(p.error());
        pieces_.emplace(std::move(*p));
    }
    return step_pieces_();
}

Ex<AssetWait> BootLadder::step_pieces_() {
    const PayloadPieces::Pass pass = pieces_->step(host_);
    if (pass == PayloadPieces::Pass::Waiting) return AssetWait::Pending;
    const std::uint64_t size = pieces_->size();
    const std::uint64_t sent = pieces_->sent();
    pieces_.reset();
    load_path_.clear();

    if (pass == PayloadPieces::Pass::Failed && sent == 0) return AssetWait::Miss;
    if (pass == PayloadPieces::Pass::Failed) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    }
    report_.bytes_downloaded += size;
    ++report_.assets_loaded;
    return AssetWait::Ready;
}

bool BootLadder::group_gates(std::uint8_t group) const {
    for (const BootAsset& row : profile_.boot_assets) {
        if (row.group == group) return row.group_required;
    }
    return false;
}

Ex<std::string> BootLadder::save_path_for(std::string_view image) {
    std::string dir = "saves/";
    dir.append(profile_.name);
    if (auto r = vfs_.ensure_dir(dir); !r) return std::unexpected(r.error());
    std::string path = dir;
    path += '/';
    std::string_view name = base_of(image);
    const auto dot = name.rfind('.');
    if (dot == std::string_view::npos) {
        path.append(name);
    } else {
        path.append(name.substr(0, dot));
    }
    path += ".sav";
    return strip_disc_word(std::move(path), profile_.staging.save_name_disc_words);
}

MountState BootLadder::mount_bounded(std::string_view path) {
    if (mount_gen_ == 0) {
        proto::FileId id{};
        if (!path.empty()) {
            auto r = host_.intern_path(path, 0);
            if (!r || *r == proto::FileId{}) return MountState::Failed;
            id = *r;
        }
        const std::uint32_t g = next_gen();

        if (!host_.order(
                proto::LinkOp::StageMount{.slot = proto::SlotIndex{profile_.staging.disc_slot.v},
                                          .same_game = report_.same_game,
                                          .path = id,
                                          .act_gen = g})) {
            return MountState::Pending;
        }
        mount_gen_ = g;
        mount_due_ns_ = now_ns() + kDiscMountBoundNs;
        return MountState::Pending;
    }
    const MountStatus s = host_.mount_status();
    if (s.generation == mount_gen_ && s.state != MountState::Pending) {
        mount_answer_ = s;
        mount_gen_ = 0;
        mount_due_ns_ = 0;
        return s.state;
    }

    if (now_ns() >= mount_due_ns_) {
        mount_gen_ = 0;
        mount_due_ns_ = 0;
        return MountState::Failed;
    }
    return MountState::Pending;
}

bool BootLadder::prepare_save(std::string_view path) {
    if (save_gen_ == 0) {
        auto id = host_.intern_path(path, 0);
        if (!id || *id == proto::FileId{}) return false;
        const std::uint32_t g = next_gen();
        if (!host_.order(
                proto::LinkOp::BindSlot{.slot = proto::SlotIndex{profile_.staging.save_slot.v},
                                        .bind = proto::LinkOp::SlotBind::Attach,
                                        .path = *id,
                                        .act_gen = g})) {
            return false;
        }
        save_gen_ = g;
        return false;
    }
    const SaveExtent e = host_.save_extent();
    if (e.generation != save_gen_) return false;
    switch (e.answer) {
        case SaveAnswer::Pending:
            return false;
        case SaveAnswer::Declined:

            save_gen_ = 0;
            return false;
        case SaveAnswer::Known:
            break;
    }
    save_size_ = e.size_bytes;
    save_gen_ = 0;
    return true;
}

bool BootLadder::order_mount_save(std::string_view path, bool bracketed) {
    proto::LinkOp::BindSlot op{.slot = proto::SlotIndex{profile_.staging.save_slot.v},
                               .bracketed = bracketed};
    if (path.empty()) {

        op.bind = proto::LinkOp::SlotBind::Unmount;
        return host_.order(op);
    }

    const std::uint64_t size = save_size_ != 0 ? save_size_ : profile_.staging.save_pre_bytes;
    auto id = host_.intern_path(path, size);
    if (!id || *id == proto::FileId{}) return false;
    op.bind = proto::LinkOp::SlotBind::Mount;
    op.path = *id;
    return host_.order(op);
}

bool BootLadder::order_announce_disc(std::uint64_t size_bytes) {
    proto::LinkOp::BindSlot op{.slot = proto::SlotIndex{profile_.staging.disc_slot.v},
                               .bind = proto::LinkOp::SlotBind::MountCd};
    if (size_bytes != 0) {
        auto id = host_.intern_path(image_, size_bytes);
        if (!id || *id == proto::FileId{}) return false;
        op.path = *id;
    }
    return host_.order(op);
}

bool BootLadder::order_notify_mount(bool loaded) {
    return host_.order(proto::LinkOp::AnnounceMount{
        .slot = proto::SlotIndex{profile_.staging.disc_slot.v}, .loaded = loaded});
}

bool BootLadder::order_status_bit0(bool asserted) {
    return host_.order(proto::LinkOp::WriteStatus{
        .start = proto::StatusBit{0}, .width = 1, .value = asserted ? 1u : 0u});
}

bool BootLadder::order_reset() { return host_.order(proto::LinkOp::StageReset{}); }

bool BootLadder::order_disc_payload(const proto::LinkOp::StageDiscPayload& facts) {
    return host_.order(facts);
}

bool BootLadder::order_empty_bracket(WideIoIndex dest) {
    return host_.order(proto::LinkOp::StagePayload{.dest = dest});
}

bool BootLadder::order_set_index(WideIoIndex dest) {
    return host_.order(proto::LinkOp::SetWideIndex{.dest = dest});
}

}  // namespace mister::cores
