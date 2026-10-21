// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/mount_queue.h"

#include <string>

#include "app/negotiated_core.h"
#include "app/owner_ladder.h"
#include "app/remembered_path.h"
#include "cores/core_profile.h"
#include "infra/message_sum.h"
#include "proto/conf_str.h"
#include "svc/vfs.h"

namespace mister::app {

MountQueue::MountQueue(const Wiring& w) noexcept
    : vfs_(w.vfs),
      inbox_(w.inbox),
      owner_events_(w.events),
      clock_(w.clock),
      ladder_(w.ladder),
      negotiated_(w.negotiated),
      owner_tally_(w.owner_tally) {}

bool MountQueue::start_mounts_live_() const noexcept {
    return !start_mounts_.empty()
        || !boot_images_.empty()
        || (save_mount_ && save_mount_->start)
        || start_pick_ladder_;
}

bool MountQueue::mount_busy_() const noexcept {
    return ladder_.live() || save_mount_.has_value();
}

void MountQueue::on_mount_image(Host& host, const MountAsk& ask) {
    TASTY_SEAT_BODY(MountQueue);
    if (mount_busy_()) {
        if (held_mount_.has_value() && (ladder_.start_armed() || start_mounts_live_())) {

            publish_refusal_(ask.which, Errc::would_block, ask.tag);
            return;
        }
        if (held_mount_.has_value()) {
            ++tally_.mounts_refused;
            publish_refusal_(ask.which, Errc::negotiation, ask.tag);
            return;
        }
        held_mount_ = ask;
        ++tally_.mounts_held;
        return;
    }
    arm_mount(host, ask);
}

void MountQueue::arm_mount(Host& host, const MountAsk& ask) {
    TASTY_SEAT_BODY(MountQueue);
    if (negotiated_.core_name().empty()) {
        ++tally_.mounts_refused;
        publish_refusal_(ask.which, Errc::core_load, ask.tag);
        return;
    }
    const cores::CoreFactory* row = negotiated_.load_row();
    const cores::SlotRole role = row != nullptr ? cores::role_of(*row->profile, ask.index)
                                                : cores::SlotRole::Image;
    if (role == cores::SlotRole::Disc || role == cores::SlotRole::Ladder) {
        arm_ladder_(host, ask, *row);
        return;
    }
    const cores::CoreProfile& p = negotiated_.loaded_profile();
    if (cores::role_of(p, ask.index) == cores::SlotRole::Save) {
        ++tally_.mounts_armed;
        const cores::StagingPolicy& sp = p.staging;
        const bool framed = sp.save_dest != proto::WideIoIndex{} && sp.save_slot == ask.index;
        MountAsk routed = ask;
        if (!framed && !routed.index_word)
            routed.index_word = pick_index_word_(ask.index, ask.path.view());
        save_mount_.emplace(
            SaveMount{
                .ask = routed,
                .due_ns = clock_.now().count() + kSaveMountBoundNs,
                .bracketed = framed
            }
        );
        return;
    }
    arm_plain_mount_(host, ask);
}

std::optional<std::uint8_t> MountQueue::pick_index_word_(
    proto::IoIndex index,
    std::string_view path
) const {
    if (!negotiated_.conf_str())
        return std::nullopt;
    const proto::ConfStrEntry* row = nullptr;
    for (const proto::ConfStrEntry& e : negotiated_.conf_str()->file_slots())
        if (e.letter == 'S' && e.ioctl_index == index.v)
            row = &e;
    if (row == nullptr)
        return std::nullopt;
    return static_cast<std::uint8_t>(
        (proto::ConfStr::ext_subindex(path, row->ext) << 6) | (row->selentry + 1)
    );
}

void MountQueue::arm_ladder_(Host& host, const MountAsk& ask, const cores::CoreFactory& row) {
    if (!ladder_.arm(ask, row, host)) {
        publish_refusal_(ask.which, Errc::core_load, ask.tag);
        ++tally_.mounts_refused;
        return;
    }
    ++tally_.mounts_armed;
}

void MountQueue::arm_plain_mount_(Host& host, const MountAsk& ask) {
    const std::string_view path = ask.path.view();
    proto::LinkOp::BindMount op{.index = ask.index, .perform = true};
    if (!path.empty()) {
        std::uint64_t size = 0;
        if (auto f = vfs_.open(path, svc::OpenMode::ReadWrite)) {
            if (auto sz = (*f)->size())
                size = sz->v;
        }
        const auto id = inbox_.intern_file_path(path, size);
        if (!id || *id == proto::FileId{}) {

            ++owner_tally_.files_failed;
            ++tally_.mounts_refused;
            (void)owner_events_.push(
                infra::make<Event>(
                    Event::InfoRequest{.id = InfoId::ImageMountFailed},
                    Event::Head{EmitSite{ERR_SITE()}, {}, ask.tag}
                )
            );
            return;
        }
        op.image = *id;
        ++owner_tally_.images_sized;
    }
    ++tally_.mounts_armed;
    if (host.order(op))
        publish_done_(ask.which, ask.tag);
}

void MountQueue::step_save_mount(Host& host) {
    TASTY_SEAT_BODY(MountQueue);
    if (!save_mount_)
        return;
    SaveMount& m = *save_mount_;
    const proto::SlotIndex slot{m.ask.index.v};
    const std::string_view path = m.ask.path.view();

    if (m.gen != 0) {
        const cores::SaveExtent e = host.save_extent();
        if (e.generation == m.gen && e.answer == cores::SaveAnswer::Declined)
            m.gen = 0;
        if (e.generation == m.gen && e.answer == cores::SaveAnswer::Known) {
            if (path.empty()) {
                finish_save_mount_(host, true);
                return;
            }
            if (m.then_load) {

                if (!negotiated_.owns_loader()
                    && host.arm_loader(*m.then_load, path, e.size_bytes)) {
                    finish_save_mount_(host, true, true);
                    return;
                }

                if (const auto id = host.intern_path(path, e.size_bytes);
                    id && *id != proto::FileId{}) {
                    ContentRequest load = *m.then_load;
                    load.save = *id;
                    host.perform_content(load);
                    finish_save_mount_(host, true, true);
                    return;
                }
            } else {
                const auto id = host.intern_path(path, e.size_bytes);
                if (id
                    && *id != proto::FileId{}
                    && host.order(
                        proto::LinkOp::BindSlot{
                            .slot = slot,
                            .bind = proto::LinkOp::SlotBind::Mount,
                            .bracketed = m.bracketed,
                            .path = *id,
                            .sends_index = m.ask.index_word.has_value(),
                            .index_word = m.ask.index_word.value_or(0)
                        }
                    )) {
                    finish_save_mount_(host, true);
                    return;
                }
            }
        }
    }
    if (clock_.now().count() >= m.due_ns) {

        if (!path.empty() && m.gen != 0)
            (void)host.order(
                proto::LinkOp::BindSlot{
                    .slot = slot,
                    .bind = proto::LinkOp::SlotBind::Detach,
                    .act_gen = host.next_generation()
                }
            );
        if (m.then_load) {

            const ContentRequest load = *m.then_load;
            ++owner_tally_.open_saves_skipped;
            host.launch_content(load);
            finish_save_mount_(host, false, true);
            return;
        }
        finish_save_mount_(host, false);
        return;
    }
    if (m.gen != 0)
        return;
    if (path.empty()) {

        if (!m.unmounted) {
            if (!host.order(
                    proto::LinkOp::BindSlot{
                        .slot = slot,
                        .bind = proto::LinkOp::SlotBind::Unmount,
                        .bracketed = m.bracketed,
                        .sends_index = m.ask.index_word.has_value(),
                        .index_word = m.ask.index_word.value_or(0)
                    }
                ))
                return;
            m.unmounted = true;
        }
        const std::uint32_t g = host.next_generation();
        if (!host.order(
                proto::LinkOp::BindSlot{
                    .slot = slot,
                    .bind = proto::LinkOp::SlotBind::Detach,
                    .act_gen = g
                }
            ))
            return;
        m.gen = g;
        return;
    }
    const auto id = host.intern_path(path, 0);
    if (!id || *id == proto::FileId{})
        return;
    const std::uint32_t g = host.next_generation();

    if (!host.order(
            proto::LinkOp::BindSlot{
                .slot = slot,
                .bind = proto::LinkOp::SlotBind::Attach,
                .manual = !m.bracketed && !m.then_load,
                .path = *id,
                .act_gen = g
            }
        ))
        return;
    m.gen = g;
}

void MountQueue::finish_save_mount_(Host& host, bool ok, bool quiet) {
    if (!save_mount_)
        return;
    const std::uint8_t slot = save_mount_->ask.index.v;
    const CorrelationTag tag = save_mount_->ask.tag;
    const UiRequest::Kind which = save_mount_->ask.which;
    quiet = quiet || save_mount_->start;
    save_mount_.reset();
    if (ok && !quiet) {
        ++tally_.save_mounts_done;
        (void)owner_events_.push(
            infra::make<Event>(
                Event::SdActivity{.slot = slot},
                Event::Head{EmitSite{ERR_SITE()}, {}, kUncaused}
            )
        );
        publish_done_(which, tag);
    } else if (!quiet) {
        (void)owner_events_.push(
            infra::make<Event>(
                Event::InfoRequest{.id = InfoId::ImageMountFailed},
                Event::Head{EmitSite{ERR_SITE()}, {}, tag}
            )
        );
    }
    arm_next_mount(host);
}

void MountQueue::arm_next_mount(Host& host) {
    TASTY_SEAT_BODY(MountQueue);
    while (!mount_busy_() && (!start_mounts_.empty() || !boot_images_.empty())) {
        if (!start_mounts_.empty()) {
            const MountAsk next = start_mounts_.front();
            start_mounts_.pop_front();
            arm_start_mount_(host, next);
            continue;
        }
        const MountAsk next = boot_images_.front();
        boot_images_.pop_front();
        arm_boot_image_(next);
    }
    if (mount_busy_() || !held_mount_.has_value())
        return;
    const MountAsk next = *held_mount_;
    held_mount_.reset();
    arm_mount(host, next);
}

void MountQueue::arm_start_mount_(Host& host, const MountAsk& ask) {
    const cores::CoreProfile& profile = negotiated_.loaded_profile();
    switch (cores::start_mount_of(profile, ask.index)) {
        case cores::StartMount::AsPick:
            ++tally_.remembered_mounts_armed;
            arm_mount(host, ask);
            start_pick_ladder_ = ladder_.live();
            return;
        case cores::StartMount::Unmodelled:
            ++tally_.remembered_mounts_skipped;
            return;
        case cores::StartMount::Generic:
            break;
    }
    switch (cores::role_of(profile, ask.index)) {
        case cores::SlotRole::Disc:
            ++tally_.remembered_mounts_skipped;
            return;
        case cores::SlotRole::Save:

            ++tally_.remembered_mounts_armed;
            ++tally_.mounts_armed;
            save_mount_.emplace(
                SaveMount{
                    .ask = ask,
                    .due_ns = clock_.now().count() + kSaveMountBoundNs,
                    .bracketed = false,
                    .start = true
                }
            );
            return;
        case cores::SlotRole::Image:
        case cores::SlotRole::Ladder:
            break;
    }
    if (!vfs_.open(ask.path.view(), svc::OpenMode::Read)) {
        ++tally_.remembered_mounts_skipped;
        return;
    }
    ++tally_.remembered_mounts_armed;
    if (cores::role_of(profile, ask.index) == cores::SlotRole::Ladder) {
        arm_mount(host, ask);
        start_pick_ladder_ = ladder_.live();
        return;
    }
    arm_plain_mount_(host, ask);
}

bool MountQueue::read_remembered_mounts(RememberedFiles files) {
    TASTY_SEAT_BODY(MountQueue);
    start_mounts_.clear();
    if (!negotiated_.conf_str() || files == RememberedFiles::Ignore)
        return false;
    bool slot0_held = false;
    const proto::ItemTable items = negotiated_.conf_str()->item_table();
    for (const proto::Item& it : items.items()) {
        if (it.kind != proto::ItemKind::MountSlot || !it.store_name || !it.has_digit)
            continue;
        const std::string path = read_remembered_path(
            vfs_,
            negotiated_.remembered_stem(),
            RememberedSlot::Mount,
            it.digit
        );
        if (path.empty())
            continue;
        slot0_held = slot0_held || it.digit == 0;
        MountAsk ask{
            .which = UiRequest::Kind::MountImage,
            .index = proto::IoIndex{it.digit},
            .tag = kUncaused
        };
        ask.index_word = static_cast<std::uint8_t>(
            (proto::ConfStr::ext_subindex(path, items.subfield(it, 1)) << 6) | it.digit
        );
        if (!ask.path.assign(path)) {
            ++tally_.remembered_mounts_skipped;
            continue;
        }
        start_mounts_.push_back(ask);
    }
    return slot0_held;
}

void MountQueue::queue_boot_images(const StartChain& chain) {
    TASTY_SEAT_BODY(MountQueue);
    boot_images_.clear();
    for (const StartFile& f : chain.boot_images()) {
        MountAsk ask{
            .which = UiRequest::Kind::MountImage,
            .index = proto::IoIndex{static_cast<std::uint8_t>(f.dest.v >> 6)},
            .path = f.path,
            .tag = kUncaused,
            .index_word = static_cast<std::uint8_t>(f.dest.v)
        };
        boot_images_.push_back(ask);
    }
}

void MountQueue::arm_boot_image_(const MountAsk& ask) {
    switch (cores::role_of(negotiated_.loaded_profile(), ask.index)) {
        case cores::SlotRole::Save:
            ++tally_.boot_images_armed;
            ++tally_.mounts_armed;
            save_mount_.emplace(
                SaveMount{
                    .ask = ask,
                    .due_ns = clock_.now().count() + kSaveMountBoundNs,
                    .bracketed = false,
                    .start = true
                }
            );
            return;
        case cores::SlotRole::Disc:
        case cores::SlotRole::Image:
        case cores::SlotRole::Ladder:
            ++tally_.boot_images_skipped;
            return;
    }
}

void MountQueue::arm_load_save(const MountAsk& ask, const ContentRequest& load) {
    TASTY_SEAT_BODY(MountQueue);
    save_mount_.emplace(
        SaveMount{.ask = ask, .due_ns = clock_.now().count() + kSaveMountBoundNs, .then_load = load}
    );
}

void MountQueue::drop() noexcept {
    TASTY_SEAT_BODY(MountQueue);
    save_mount_.reset();
    held_mount_.reset();
    start_mounts_.clear();
    boot_images_.clear();
    start_pick_ladder_ = false;
}

void MountQueue::ladder_ended() noexcept {
    TASTY_SEAT_BODY(MountQueue);
    start_pick_ladder_ = false;
}

void MountQueue::publish_refusal_(UiRequest::Kind which, Errc code, CorrelationTag tag) noexcept {
    (void)owner_events_.push(
        infra::make<Event>(
            Event::RequestRefused{.which = which, .why = code},
            Event::Head{EmitSite{ERR_SITE()}, {}, tag}
        )
    );
}

void MountQueue::publish_done_(UiRequest::Kind which, CorrelationTag tag) noexcept {
    if (tag == kUncaused)
        return;
    (void)owner_events_.push(
        infra::make<Event>(
            Event::RequestDone{.which = which},
            Event::Head{EmitSite{ERR_SITE()}, {}, tag}
        )
    );
}

}
