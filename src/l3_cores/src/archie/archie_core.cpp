// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/archie/archie_core.h"

#include <cstring>

#include "infra/byte_view.h"
#include "infra/indexed_array.h"
#include "infra/indexed_span.h"
#include "infra/path_parts.h"
#include "proto/link/core_session.h"
#include "proto/status_edit.h"

namespace mister::cores::archie {

ArchieCore::ArchieCore(const CoreProfile& p, const HostServices& h) : Core(p, h) {}

Ex<void> ArchieCore::do_adopt(const CoreAdopt& a) {
    session_ = infra::OptRef<proto::link::CoreSession>{a.session};
    return {};
}

Ex<void> ArchieCore::on_mount(IoIndex slot, const MountedPath& p) {
    if (slot == kRomSlot) {
        (void)cfg_.rom_img.assign(p.path);
        return {};
    }
    if (slot == kFloppy0Slot || slot == kFloppy1Slot) {
        (void)floppy_img_[slot == kFloppy0Slot ? 0 : 1].assign(p.path);
        return {};
    }
    return std::unexpected(slot.refusal(Errc::slot_range, ERR_SITE()));
}

std::string_view ArchieCore::file_text_(ArchieOption o) const noexcept {
    switch (o) {
        case ArchieOption::Floppy0:
            return floppy_img_[0].view();
        case ArchieOption::Floppy1:
            return floppy_img_[1].view();
        case ArchieOption::OsRom:
            return cfg_.rom_img.view();
        case ArchieOption::None:
        case ArchieOption::Aspect:
        case ArchieOption::Scale:
        case ArchieOption::Refresh:
        case ArchieOption::StereoMix:
        case ArchieOption::AudioFix:
        case ArchieOption::JoySwap:
        case ArchieOption::kCount:
            break;
    }
    return {};
}

OptionRowView ArchieCore::option_view(proto::OptionRowIndex index) noexcept {
    TASTY_SEAT_BODY(ArchieCore);
    const infra::IndexedSpan<const OptionRow, proto::OptionRowIndex> rows{profile().option_rows};
    const auto found = rows.find(index);
    if (!found)
        return {};
    const OptionRow& r = *found;
    switch (r.kind()) {
        case OptionRow::Kind::Cycle: {
            const OptionRow::CycleArm* cycle = r.cycle_arm();
            if (cycle == nullptr || !session_)
                return {};
            const proto::FieldValue cur = cycle->bits.read(session_->status().value());
            return OptionRowView::cycle(cur, cycle->value_name(cur));
        }
        case OptionRow::Kind::File: {
            const auto o = option_of(r.id);
            if (!o)
                return {};
            const std::string_view path = file_text_(*o);
            return OptionRowView{
                .current = path.empty() ? std::uint8_t{0} : std::uint8_t{1},
                .dimmed = false,
                .text = infra::basename(path)
            };
        }
        case OptionRow::Kind::Action:
        case OptionRow::Kind::Link:
        case OptionRow::Kind::Host:
        case OptionRow::Kind::Separator:
        case OptionRow::Kind::kCount:
            return {};
    }
    return {};
}

Ex<void> ArchieCore::set_option(proto::OptionRowIndex index, proto::FieldValue choice) {
    TASTY_SEAT_BODY(ArchieCore);
    const infra::IndexedSpan<const OptionRow, proto::OptionRowIndex> rows{profile().option_rows};
    const auto found = rows.find(index);
    if (!found || !session_)
        return std::unexpected(index.refusal(Errc::slot_range, ERR_SITE()));
    const OptionRow::CycleArm* cycle = found->cycle_arm();
    if (cycle == nullptr || !cycle->bits.valid() || !choice.within(cycle->choice_count())) {
        return std::unexpected(index.refusal(Errc::slot_range, ERR_SITE()));
    }
    return session_->apply(proto::StatusEdit::field(cycle->bits, choice));
}

SystemCtrl ArchieCore::live_ctrl_() const noexcept {
    SystemCtrl ctrl = cfg_.system_ctrl;
    if (!session_)
        return ctrl;
    const proto::StatusWord status = session_->status().value();
    for (const SystemCtrl::Home& h : SystemCtrl::kHomes)
        ctrl = ctrl.with(h, h.status.read(status));
    return ctrl;
}

Ex<void> ArchieCore::apply(ConfigBlob value) {
    TASTY_SEAT_BODY(ArchieCore);
    ArchieConfig next{};
    if (!value.bytes.empty()) {
        if (value.bytes.size() != infra::bytes_of(next).size()) {
            return std::unexpected(
                proto::FileSize::of(value.bytes).refusal(Errc::bad_format, ERR_SITE())
            );
        }
        std::memcpy(&next, value.bytes.data(), sizeof next);
    }
    cfg_ = next;
    if (!session_)
        return {};
    proto::StatusEdit edit{};
    for (const SystemCtrl::Home& h : SystemCtrl::kHomes)
        edit = edit.then(proto::StatusEdit::field(h.status, cfg_.system_ctrl.get(h)));
    return session_->apply(edit);
}

std::span<const std::byte> ArchieCore::value(std::span<std::byte> dst) const noexcept {
    TASTY_SEAT_BODY(ArchieCore);
    if (dst.size() < infra::bytes_of(cfg_).size())
        return {};
    ArchieConfig v = cfg_;
    v.system_ctrl = live_ctrl_();
    std::memcpy(dst.data(), &v, sizeof v);
    return dst.first(sizeof v);
}

std::optional<mra::NvramSave> ArchieCore::save_image() const noexcept {
    TASTY_SEAT_BODY(ArchieCore);
    return mra::NvramSave{.index = kCmos.index, .size = proto::FileSize::of(kCmos.bytes)};
}

}
