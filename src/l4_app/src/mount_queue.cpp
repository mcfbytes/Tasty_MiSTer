// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/mount_queue.h"

#include <string>

#include "app/negotiated_core.h"
#include "app/owner_ladder.h"
#include "app/remembered_path.h"
#include "cores/core_profile.h"
#include "infra/message_sum.h"
#include "infra/prelude.h"
#include "proto/conf_str.h"
#include "svc/vfs.h"

namespace mister::app {

MountQueue::MountQueue(const Wiring& w) noexcept
    : vfs_(w.vfs),
      card_(w.vfs),
      inbox_(w.inbox),
      owner_events_(w.events.producer()),
      refusals_(w.refusals),
      clock_(w.clock),
      ladder_(w.ladder),
      negotiated_(w.negotiated),
      owner_tally_(w.owner_tally),
      window_map_(w.window_map),
      aperture_(w.aperture) {}

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
    const auto row = negotiated_.load_row();
    const cores::SlotRole role = row ? cores::role_of(row->profile, ask.index)
                                     : cores::SlotRole::Image;
    if (role == cores::SlotRole::Disc || role == cores::SlotRole::Ladder) {
        arm_ladder_(host, ask, *row);
        return;
    }
    const cores::CoreProfile& p = negotiated_.loaded_profile();
    const bool serviced = cores::mounts_via_slot_service(p.slots, ask.index);
    if (cores::slot_service_serves(cores::role_of(p, ask.index)) || serviced) {
        ++tally_.mounts_armed;
        const cores::StagingPolicy& sp = p.staging;
        const bool framed = sp.save && sp.save->bracket && sp.save->slot == ask.index;
        MountAsk routed = ask;
        if (!framed && !routed.index_word && ask.servant == MountAsk::Servant::None)
            routed.index_word = pick_index_word_(ask.index, ask.path.view());
        save_mount_.emplace(
            SaveMount{
                .ask = routed,
                .due = os::Deadline::in(clock_, kSaveMountBound),
                .bracketed = framed,
                .class_follows = serviced
            }
        );
        return;
    }
    arm_plain_mount_(host, ask);
}

std::optional<proto::FioIndexByte> MountQueue::pick_index_word_(
    proto::IoIndex index,
    std::string_view path
) const {
    if (!negotiated_.conf_str())
        return std::nullopt;
    const proto::ConfStrEntry* row = nullptr;
    for (const proto::ConfStrEntry& e : negotiated_.conf_str()->file_slots().rows())
        if (e.file_row() == proto::FileRowId::mount(index))
            row = &e;
    if (row == nullptr)
        return std::nullopt;
    const proto::IoIndex entry{static_cast<std::uint8_t>(row->selentry + 1)};
    return proto::WideIoIndex::of(entry, proto::ext_subindex(path, row->ext)).fio_byte();
}

void MountQueue::arm_ladder_(Host& host, const MountAsk& ask, const cores::CoreFactory& row) {
    if (!ladder_.arm(ask, row, host.ladder_host())) {
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
        proto::FileSize size{};
        if (auto f = card_.open(path, svc::OpenMode::ReadWrite)) {
            if (auto sz = (*f)->size())
                size = *sz;
        }
        const auto id = inbox_.intern_file_path(path, size);
        if (!id) {

            ++owner_tally_.files_failed;
            ++tally_.mounts_refused;
            owner_events_.push(
                Event::InfoRequest{.id = InfoId::ImageMountFailed},
                Event::Head{ERR_SITE(), {}, ask.tag}
            );
            return;
        }
        op.image = *id;
        ++owner_tally_.images_sized;
    }
    ++tally_.mounts_armed;
    if (host.ladder_host().order(op))
        publish_done_(ask.which, ask.tag);
}

void MountQueue::step_save_mount(Host& host) {
    TASTY_SEAT_BODY(MountQueue);
    if (!save_mount_)
        return;
    const proto::SlotIndex slot = save_mount_->ask.index.block_slot();

    if (on_known_(host, slot))
        return;
    SaveMount& m = *save_mount_;

    if (m.step != SaveMount::Step::Mirror && m.due.expired(clock_)) {
        on_timeout_(host, slot);
        return;
    }
    switch (m.step) {
        case SaveMount::Step::Unmount:
            advance_unmount_(host, slot);
            return;
        case SaveMount::Step::Act:
            if (m.gen)
                return;
            if (m.ask.path.view().empty()) {
                advance_detach_(host, slot);
            } else {
                advance_attach_(host, slot);
            }
            return;
        case SaveMount::Step::Mirror:
            advance_mirror_();
            return;
        case SaveMount::Step::Announce:
        case SaveMount::Step::ClassHalf:
            return;
    }
}

bool MountQueue::on_known_(Host& host, proto::SlotIndex slot) {
    if (!save_mount_)
        return false;
    SaveMount& m = *save_mount_;
    if (m.step == SaveMount::Step::Act) {
        if (!m.gen)
            return false;
        const cores::SaveExtent e = host.ladder_host().save_extent();
        if (e.act != m.gen)
            return false;
        if (e.answer == cores::SaveVerdict::Declined)
            m.gen = {};
        if (e.answer != cores::SaveVerdict::Known)
            return false;
        m.bytes = e.size_bytes;

        if (m.ask.path.view().empty()) {
            m.step = SaveMount::Step::ClassHalf;
        } else {
            begin_mirror_(m);
        }
    }
    if (m.step == SaveMount::Step::Announce) {
        if (m.then_load)
            return hand_load_(host);
        if (!announce_(host, slot))
            return false;
        m.step = SaveMount::Step::ClassHalf;
    }
    if (m.step != SaveMount::Step::ClassHalf)
        return false;
    if (m.class_follows) {

        const bool ordered = m.ask.path.view().empty() ? order_class_eject_(host, m.ask.index)
                                                       : order_class_mount_(host, m.ask, m.bytes);
        if (!ordered)
            return false;
    }
    finish_save_mount_(host, true);
    return true;
}

bool MountQueue::hand_load_(Host& host) {
    if (!save_mount_ || !save_mount_->then_load)
        return false;
    SaveMount& m = *save_mount_;
    const std::string_view path = m.ask.path.view();

    if (!negotiated_.owns_loader() && host.arm_loader(*m.then_load, path, m.bytes)) {
        finish_save_mount_(host, true, true);
        return true;
    }

    const auto id = host.ladder_host().intern_path(path, m.bytes);
    if (!id)
        return false;
    ContentRequest load = *m.then_load;
    load.save = *id;
    host.perform_content(load);
    finish_save_mount_(host, true, true);
    return true;
}

bool MountQueue::announce_(Host& host, proto::SlotIndex slot) {
    if (!save_mount_)
        return false;
    const SaveMount& m = *save_mount_;

    if (m.ask.servant == MountAsk::Servant::Control)
        return host.ladder_host().order(
            proto::LinkOp::BindSlot{.slot = slot, .bind = proto::LinkOp::SlotBind::Announce}
        );
    const auto id = host.ladder_host().intern_path(m.ask.path.view(), m.bytes);
    if (!id)
        return false;
    return host.ladder_host().order(
        proto::LinkOp::BindSlot{
            .slot = slot,
            .bind = proto::LinkOp::SlotBind::Mount,
            .bracketed = m.bracketed,
            .path = *id,
            .sends_index = m.ask.index_word.has_value(),
            .index_word = m.ask.index_word.value_or(proto::FioIndexByte{})
        }
    );
}

bool MountQueue::order_class_mount_(Host& host, const MountAsk& ask, proto::FileSize bytes) {
    const auto id = host.ladder_host().intern_path(ask.path.view(), bytes);
    if (!id)
        return false;
    return host.ladder_host().order(
        proto::LinkOp::BindMount{.index = ask.index, .perform = true, .image = *id}
    );
}

bool MountQueue::order_class_eject_(Host& host, proto::IoIndex index) {
    return host.ladder_host().order(proto::LinkOp::BindMount{.index = index, .perform = true});
}

void MountQueue::on_timeout_(Host& host, proto::SlotIndex slot) {
    if (!save_mount_)
        return;
    SaveMount& m = *save_mount_;
    const bool mount = !m.ask.path.view().empty();
    if (mount && m.gen)
        (void)host.ladder_host().order(
            proto::LinkOp::DetachSave{.slot = slot, .act = host.ladder_host().next_act()}
        );
    if (m.then_load) {

        const ContentRequest load = *m.then_load;
        ++owner_tally_.open_saves_skipped;
        host.launch_content(load);
        finish_save_mount_(host, false, true);
        return;
    }

    const bool announced = mount && m.step == SaveMount::Step::ClassHalf;
    if (m.class_follows && !announced)
        (void)order_class_eject_(host, m.ask.index);
    finish_save_mount_(host, false);
}

void MountQueue::advance_unmount_(Host& host, proto::SlotIndex slot) {
    if (!save_mount_)
        return;
    SaveMount& m = *save_mount_;
    if (!host.ladder_host().order(
            proto::LinkOp::BindSlot{
                .slot = slot,
                .bind = proto::LinkOp::SlotBind::Unmount,
                .bracketed = m.bracketed,
                .sends_index = m.ask.index_word.has_value(),
                .index_word = m.ask.index_word.value_or(proto::FioIndexByte{})
            }
        ))
        return;
    m.step = SaveMount::Step::Act;
    advance_detach_(host, slot);
}

void MountQueue::advance_detach_(Host& host, proto::SlotIndex slot) {
    if (!save_mount_)
        return;
    const proto::ActKey g = host.ladder_host().next_act();
    if (!host.ladder_host().order(proto::LinkOp::DetachSave{.slot = slot, .act = g}))
        return;
    save_mount_->gen = g;
}

void MountQueue::advance_attach_(Host& host, proto::SlotIndex slot) {
    if (!save_mount_)
        return;
    SaveMount& m = *save_mount_;
    const std::string_view path = m.ask.path.view();
    const auto id = host.ladder_host().intern_path(path, proto::FileSize{});
    if (!id)
        return;
    const proto::ActKey g = host.ladder_host().next_act();

    (void)refusal_reader_.take_if_changed(refusals_);

    if (!host.ladder_host().order(
            proto::LinkOp::AttachSave{
                .slot = slot,
                .manual = !m.bracketed && !m.then_load,
                .path = *id,
                .act = g
            }
        ))
        return;
    m.gen = g;
}

void MountQueue::begin_mirror_(SaveMount& m) {
    m.step = SaveMount::Step::Announce;
    const cores::CoreProfile& p = negotiated_.loaded_profile();
    const std::optional<cores::DiskMirror> mirror = cores::mirror_of(p.slots, m.ask.index);
    if (!mirror)
        return;
    auto source = svc::open_mirror_source(
        vfs_,
        m.ask.path.view(),
        cores::disk_formats_of(p, m.ask.index),
        mirror->room
    );
    if (!source) {
        tally_.mirror_fills_refused.add();
        return;
    }
    if (*source == nullptr) {
        tally_.mirror_fills_skipped.add();
        return;
    }
    auto fill = MirrorFill::open(std::move(*source), *mirror, p.windows, aperture_, window_map_);
    if (!fill) {
        tally_.mirror_fills_refused.add();
        return;
    }
    m.fill.emplace(std::move(*fill));
    m.step = SaveMount::Step::Mirror;
    tally_.mirror_fills.add();
}

void MountQueue::advance_mirror_() {
    if (!save_mount_ || !save_mount_->fill)
        return;
    SaveMount& m = *save_mount_;
    auto done = m.fill->step();
    if (!done || *done) {
        if (!done)
            tally_.mirror_fills_refused.add();
        m.fill.reset();
        m.step = SaveMount::Step::Announce;
        m.due = os::Deadline::in(clock_, kSaveMountBound);
    }
}

void MountQueue::finish_save_mount_(Host& host, bool ok, bool quiet) {
    if (!save_mount_)
        return;
    const proto::IoIndex index = save_mount_->ask.index;
    const CorrelationTag tag = save_mount_->ask.tag;
    const UiRequest::Kind which = save_mount_->ask.which;
    quiet = quiet || save_mount_->start;
    const bool servant = save_mount_->ask.servant != MountAsk::Servant::None;
    quiet = quiet || servant;
    const proto::SlotIndex slot = index.block_slot();
    const bool mounting = ok && !save_mount_->ask.path.view().empty();
    const std::optional<svc::MountRefusal> refused = mounting ? refusal_at_(slot)
                                                              : std::optional<svc::MountRefusal>{};
    if (save_mount_->ask.servant == MountAsk::Servant::Control) {
        if (ok)
            announced_ = announced_.with(slot);
    } else if (const auto held = mounted_.find(slot)) {
        *held = ok && !refused ? save_mount_->ask.path : PathText{};
    }
    save_mount_.reset();
    if (refused) {

        if (!servant) {
            owner_events_.push(
                Event::MountRefused{.slot = slot, .reason = *refused},
                Event::Head{ERR_SITE(), {}, tag}
            );
        }
    } else if (ok && !quiet) {
        ++tally_.save_mounts_done;
        owner_events_.push(
            Event::SdActivity{.index = proto::WideIoIndex::of(index).fio_byte()},
            Event::Head{ERR_SITE(), {}, kUncaused}
        );
        publish_done_(which, tag);
    } else if (!quiet) {
        owner_events_.push(
            Event::InfoRequest{.id = InfoId::ImageMountFailed},
            Event::Head{ERR_SITE(), {}, tag}
        );
    }
    arm_next_mount(host);
}

std::optional<svc::MountRefusal> MountQueue::refusal_at_(proto::SlotIndex slot) noexcept {
    const std::optional<svc::MountRefusalNote> note = refusal_reader_.take_if_changed(refusals_);
    if (!note || note->slot != slot)
        return std::nullopt;
    return note->reason;
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
        case cores::SlotRole::Drive:

            ++tally_.remembered_mounts_armed;
            ++tally_.mounts_armed;
            save_mount_.emplace(
                SaveMount{
                    .ask = ask,
                    .due = os::Deadline::in(clock_, kSaveMountBound),
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
        if (it.kind != proto::ItemKind::MountSlot || !it.store_name || !it.digit)
            continue;
        const proto::FileSlotDigit digit = *it.digit;
        const std::string path = read_remembered_path(
            vfs_,
            negotiated_.remembered_stem(),
            proto::FileRowId::mount(digit.io_index())
        );
        if (path.empty())
            continue;

        slot0_held = slot0_held || digit == proto::FileSlotDigit::zero();
        const proto::IoIndex io = digit.io_index();
        MountAsk ask{.which = UiRequest::Kind::MountImage, .index = io, .tag = kUncaused};

        if (cores::mount_index_form_of(negotiated_.named_profile().slots, io)
            == cores::MountIndexForm::ExtOrdinal) {
            ask.index_word = proto::WideIoIndex::of(
                                 io,
                                 proto::ext_subindex(path, items.subfield(it, 1))
            )
                                 .fio_byte();
        }
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
            .index = f.dest.sub().slot_index(),
            .path = f.path,
            .tag = kUncaused,
            .index_word = f.dest.fio_byte()
        };
        boot_images_.push_back(ask);
    }
}

void MountQueue::arm_boot_image_(const MountAsk& ask) {
    switch (cores::role_of(negotiated_.loaded_profile(), ask.index)) {
        case cores::SlotRole::Save:
        case cores::SlotRole::Drive:
            ++tally_.boot_images_armed;
            ++tally_.mounts_armed;
            save_mount_.emplace(
                SaveMount{
                    .ask = ask,
                    .due = os::Deadline::in(clock_, kSaveMountBound),
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
        SaveMount{.ask = ask, .due = os::Deadline::in(clock_, kSaveMountBound), .then_load = load}
    );
}

void MountQueue::drop() noexcept {
    TASTY_SEAT_BODY(MountQueue);
    save_mount_.reset();
    held_mount_.reset();
    start_mounts_.clear();
    boot_images_.clear();
    start_pick_ladder_ = false;
    mounted_ = {};
    announced_ = {};
}

void MountQueue::ladder_ended() noexcept {
    TASTY_SEAT_BODY(MountQueue);
    start_pick_ladder_ = false;
}

void MountQueue::publish_refusal_(UiRequest::Kind which, Errc code, CorrelationTag tag) noexcept {
    owner_events_.push(
        Event::RequestRefused{.which = which, .why = code},
        Event::Head{ERR_SITE(), {}, tag}
    );
}

void MountQueue::publish_done_(UiRequest::Kind which, CorrelationTag tag) noexcept {
    if (tag == kUncaused)
        return;
    owner_events_.push(Event::RequestDone{.which = which}, Event::Head{ERR_SITE(), {}, tag});
}

}
