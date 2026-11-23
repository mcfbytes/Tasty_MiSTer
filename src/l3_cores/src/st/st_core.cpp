// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/st/st_core.h"
#include "cores/core_adopt.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <utility>

#include "cores/disk_port.h"
#include "cores/st/acsi_poll.h"
#include "cores/st/st_media_fact.h"
#include "infra/byte_view.h"
#include "infra/indexed_array.h"
#include "infra/indexed_span.h"
#include "infra/prelude.h"
#include "proto/link/link.h"
#include "proto/link/spi_frame.h"
#include "proto/link/download_session.h"
#include "proto/status_word.h"
#include "proto/uio_opcodes.h"

namespace mister::cores::st {

namespace {

constexpr auto kDiskSlots = std::array{
    st_ctrl::StSlot::FloppyA,
    st_ctrl::StSlot::FloppyB,
    st_ctrl::StSlot::Acsi0,
    st_ctrl::StSlot::Acsi1
};

[[nodiscard]] constexpr proto::FileSize file_size_of(std::span<const std::uint8_t> bytes) noexcept {
    return proto::FileSize{bytes.size()};
}

[[nodiscard]] constexpr proto::FileSize file_size_of(std::span<const std::byte> bytes) noexcept {
    return proto::FileSize{bytes.size()};
}

static_assert(
    st_ctrl::CtrlWord::of(
        proto::BitField{proto::StatusBit{31}, 9}
            .write(st_ctrl::CtrlWord{}.status_word(), proto::FieldValue{0x1FF})
    )
    == st_ctrl::CtrlWord{0x8000'0000u}
);
static_assert(
    proto::BitField{proto::StatusBit{32}, 1}.read(st_ctrl::CtrlWord{0xFFFF'FFFFu}.status_word())
    == proto::FieldValue{}
);

}

StCore::StCore(const CoreProfile& p, const HostServices& h) : Core(p, h) {}

Ex<void> StCore::push_control_() {
    proto::link::SpiFrame frame{host().link};
    if (auto r = frame.begin(proto::UioOp::SetStatus2); !r)
        return r;
    const hal::SpiHalves sys = cfg_.system_ctrl.wire_halves();
    const hal::SpiHalves ext = hal::SpiHalves::of(cfg_.ext_ctrl);
    const std::array<hal::SpiWord, 4> payload = {sys.low, sys.high, ext.low, ext.high};
    for (const hal::SpiWord w : payload) {
        if (auto r = frame.word(w); !r)
            return r;
    }
    if (auto r = frame.end(); !r)
        return r;
    ++control_pushes_;
    return {};
}

Ex<void> StCore::update_sysctrl_(st_ctrl::CtrlWord ctrl) {
    store_sysctrl_(ctrl);
    return push_control_();
}

void StCore::store_sysctrl_(st_ctrl::CtrlWord ctrl) noexcept {
    cfg_.system_ctrl = ctrl;
}

Ex<void> StCore::set_system_ctrl(std::uint32_t ctrl) {
    return update_sysctrl_(st_ctrl::CtrlWord{ctrl});
}

Ex<void> StCore::set_ext_ctrl(std::uint32_t ext) {
    cfg_.ext_ctrl = ext;
    return push_control_();
}

Ex<void> StCore::do_adopt(const CoreAdopt&) {
    if (!acsi_port_())
        return std::unexpected(Error{Errc::not_found, ERR_SITE(), 0});
    return {};
}

infra::OptRef<IDiskPort> StCore::acsi_port_() const noexcept {
    return host().disk[proto::DiskProtocol::Acsi];
}

Ex<void> StCore::eject_all_() {
    for (const st_ctrl::StSlot slot : kDiskSlots) {
        if (auto r = insert_disk_(slot, {}); !r)
            return r;
    }
    return {};
}

Ex<void> StCore::on_apply_load_facts(std::span<const std::uint8_t> facts, std::string_view) {
    const auto f = StMediaFact::of(facts);
    if (!f) {
        return std::unexpected(
            Error{Errc::bad_format, ERR_SITE(), static_cast<std::uint32_t>(facts.size())}
        );
    }
    switch (f->verb) {
        case StMediaFact::Verb::Hold:

            if (const auto disk = acsi_port_())
                disk->abandon_command();
            if (auto r = update_sysctrl_(cfg_.system_ctrl.with(st_ctrl::CtrlWord::kCpuReset)); !r) {
                return r;
            }
            if (!f->cold) {
                ++warm_resets_;
                return {};
            }
            ++cold_resets_;
            return {};
        case StMediaFact::Verb::NoTos:
            ++no_tos_refusals_;
            return {};
        case StMediaFact::Verb::Release:
            return update_sysctrl_(cfg_.system_ctrl.without(st_ctrl::CtrlWord::kCpuReset));
        case StMediaFact::Verb::kCount:
            break;
    }
    return std::unexpected(f->refusal(Errc::bad_format, ERR_SITE()));
}

Ex<void> StCore::insert_disk_(st_ctrl::StSlot slot, std::string_view path) {
    if (const auto target = slot.acsi_target()) {

        if (!path.empty())
            return std::unexpected(slot.refusal(Errc::slot_range, ERR_SITE()));
        return eject_hdd_(*target);
    }
    if (!slot.is_floppy())
        return std::unexpected(slot.refusal(Errc::slot_range, ERR_SITE()));
    FixedStr<kPathBytes, StrFit::Clip>& img = infra::at_index(floppy_img_, slot);
    (void)img.assign(path);

    const st_ctrl::CtrlWord sc = cfg_.system_ctrl;
    const st_ctrl::CtrlWord::Bit bit = st_ctrl::write_protect_bit(slot);
    store_sysctrl_(img.empty() ? sc.with(bit) : sc.without(bit));
    return push_control_();
}

Ex<void> StCore::eject_hdd_(AcsiTarget target) {
    const infra::OptRef<IDiskPort> disk = acsi_port_();
    if (disk)
        disk->abandon_command_on(target.unit());
    (void)cfg_.acsi_img[target].assign({});
    store_sysctrl_(cfg_.system_ctrl.without(st_ctrl::acsi_enable_bit(target)));
    acsi_[target] = AcsiBind{};
    auto pushed = push_control_();
    if (disk)
        disk->order_bind(target.unit(), {});
    return pushed;
}

MountState StCore::mount_slot(proto::SlotIndex slot, std::string_view path) {
    TASTY_SEAT_BODY(StCore);
    const auto disk_slot = st_ctrl::StSlot::of(slot.io_index());
    if (!disk_slot)
        return MountState::Failed;
    const auto target = disk_slot->acsi_target();
    if (!target)
        return MountState::Failed;
    const infra::OptRef<IDiskPort> port = acsi_port_();
    if (!port)
        return MountState::Failed;
    IDiskPort& disk = *port;
    if (!acsi_[*target].out || cfg_.acsi_img[*target].view() != path) {
        (void)cfg_.acsi_img[*target].assign(path);
        acsi_[*target] = AcsiBind{.bound = false, .out = true};
        disk.order_bind(target->unit(), path);
    }
    const MountState answer = disk.bind_answer(target->unit());
    if (answer == MountState::Pending)
        return MountState::Pending;
    acsi_[*target].out = false;
    const st_ctrl::CtrlWord::Bit enable = st_ctrl::acsi_enable_bit(*target);
    acsi_[*target].bound = answer == MountState::Done && !path.empty();
    const st_ctrl::CtrlWord sc = cfg_.system_ctrl;
    store_sysctrl_(acsi_[*target].bound ? sc.with(enable) : sc.without(enable));
    if (auto r = push_control_(); !r)
        return MountState::Failed;
    return answer;
}

Ex<proto::SessionParams> StCore::stream_opening(const StreamAsk& ask) {
    TASTY_SEAT_BODY(StCore);
    return stream_.stream_opening(StreamAsk{.index = ask.index});
}

void StCore::stream_closed(const StreamLoadEnd& end) noexcept {
    TASTY_SEAT_BODY(StCore);
    stream_.stream_closed(end);
}

Ex<void> StCore::on_mount(IoIndex slot, const MountedPath& p) {
    const auto disk_slot = st_ctrl::StSlot::of(slot);
    if (!disk_slot)
        return std::unexpected(slot.refusal(Errc::slot_range, ERR_SITE()));
    if (disk_slot->is_disk())
        return insert_disk_(*disk_slot, p.path);
    if (*disk_slot == st_ctrl::StSlot::Tos) {

        (void)cfg_.tos_img.assign(p.path);
        return {};
    }
    if (*disk_slot == st_ctrl::StSlot::Cart) {

        (void)cart_img_.assign(p.path);
        return {};
    }
    return std::unexpected(slot.refusal(Errc::slot_range, ERR_SITE()));
}

Ex<void> StCore::apply(ConfigBlob value) {
    TASTY_SEAT_BODY(StCore);
    StConfig next = StConfig::defaults();
    if (!value.bytes.empty()) {
        if (value.bytes.size() != infra::bytes_of(next).size()) {
            return std::unexpected(file_size_of(value.bytes).refusal(Errc::bad_format, ERR_SITE()));
        }
        std::memcpy(&next, value.bytes.data(), sizeof next);
    }
    if (auto r = eject_all_(); !r)
        return r;
    cfg_ = next;
    if (!cfg_.cart_img.empty())
        (void)cart_img_.assign(cfg_.cart_img.view());
    return {};
}

std::span<const std::byte> StCore::value(std::span<std::byte> dst) const noexcept {
    TASTY_SEAT_BODY(StCore);
    if (dst.size() < infra::bytes_of(cfg_).size())
        return {};
    StConfig v = cfg_;
    (void)v.cart_img.assign(cart_img_.view());
    std::memcpy(dst.data(), &v, sizeof v);
    return dst.first(sizeof v);
}

st_ctrl::CtrlWord StCore::word_(WordId w) const noexcept {
    return w == WordId{0}
        ? cfg_.system_ctrl
        : st_ctrl::CtrlWord::load_raw(std::span<const std::uint32_t, 1>(&cfg_.ext_ctrl, 1));
}

Ex<void> StCore::push_word_(WordId w, st_ctrl::CtrlWord v) {
    if (w == WordId{0})
        return update_sysctrl_(v);

    std::uint32_t ext = 0;
    v.store_raw(std::span<std::uint32_t, 1>(&ext, 1));
    return set_ext_ctrl(ext);
}

OptionRowView StCore::option_view(proto::OptionRowIndex index) noexcept {
    TASTY_SEAT_BODY(StCore);
    const infra::IndexedSpan<const OptionRow, proto::OptionRowIndex> rows{profile().option_rows};
    const auto found = rows.find(index);
    if (!found)
        return {};
    const OptionRow& r = *found;
    switch (r.kind()) {
        case OptionRow::Kind::Cycle: {
            const OptionRow::CycleArm* cycle = r.cycle_arm();
            if (cycle == nullptr)
                return {};
            if (!cycle->bits.valid()) {
                const auto o = option_of(r.id);
                if (!o)
                    return {};
                return StOptionRow::of(*o).cycle_view(facts_(), *cycle);
            }
            const proto::FieldValue cur = cycle->bits.read(word_(cycle->word).status_word());
            return OptionRowView::cycle(cur, cycle->value_name(cur));
        }
        case OptionRow::Kind::File: {
            const auto o = option_of(r.id);
            if (!o)
                return {};
            return StOptionRow::of(*o).file_view(facts_(), view_text_);
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

StOptionRow::Facts StCore::facts_() const noexcept {
    StOptionRow::Facts f{
        .system_ctrl = cfg_.system_ctrl,
        .ext_ctrl = cfg_.ext_ctrl,
        .floppy =
            {infra::at_index(floppy_img_, st_ctrl::StSlot::FloppyA).view(),
             infra::at_index(floppy_img_, st_ctrl::StSlot::FloppyB).view()},
        .tos = cfg_.tos_img.view(),
        .cart = cart_img_.view()
    };

    for (std::uint8_t t = 0; t < kAcsiTargets; ++t) {
        const AcsiTarget target{t};
        f.acsi[target] = StOptionRow::AcsiFact{
            .bound = acsi_[target].bound,
            .path = cfg_.acsi_img[target].view()
        };
    }
    return f;
}

Ex<void> StCore::set_option(proto::OptionRowIndex index, proto::FieldValue choice) {
    TASTY_SEAT_BODY(StCore);
    const infra::IndexedSpan<const OptionRow, proto::OptionRowIndex> rows{profile().option_rows};
    const auto found = rows.find(index);
    if (!found) {
        return std::unexpected(index.refusal(Errc::slot_range, ERR_SITE()));
    }
    const OptionRow& r = *found;
    const auto o = option_of(r.id);
    if (!o)
        return std::unexpected(r.id.refusal(Errc::slot_range, ERR_SITE()));
    switch (r.kind()) {
        case OptionRow::Kind::Cycle: {
            const OptionRow::CycleArm* cycle = r.cycle_arm();
            if (cycle == nullptr)
                break;

            if (!choice.within(cycle->choice_count())) {
                return std::unexpected(OptionDetail::of(choice, index).refuse(ERR_SITE()));
            }
            if (!cycle->bits.valid()) {
                const auto w = StOptionRow::of(*o).cycle_set(facts_(), choice);
                if (!w)
                    return std::unexpected(w.error());
                return apply_write_(*w);
            }
            const proto::StatusWord next = cycle->bits.write(
                word_(cycle->word).status_word(),
                choice
            );
            return push_word_(cycle->word, st_ctrl::CtrlWord::of(next));
        }
        case OptionRow::Kind::Action:
        case OptionRow::Kind::File:
        case OptionRow::Kind::Link:
        case OptionRow::Kind::Host:
        case OptionRow::Kind::Separator:
        case OptionRow::Kind::kCount:
            break;
    }
    return std::unexpected(index.refusal(Errc::slot_range, ERR_SITE()));
}

Ex<void> StCore::apply_write_(const StOptionRow::Write& w) {
    if (w.system_ctrl) {
        if (auto r = update_sysctrl_(*w.system_ctrl); !r)
            return r;
    }
    if (w.ext_ctrl)
        return set_ext_ctrl(*w.ext_ctrl);
    return {};
}

void StCore::service_kick() noexcept {
    TASTY_SEAT_BODY(StCore);
    if (const auto disk = acsi_port_()) {
        proto::link::Link link{host().link};
        disk->service_rt(link, kAcsiPoll);
    }
}

bool StCore::acsi_bound(AcsiTarget target) const noexcept {
    const auto bind = acsi_.find(target);
    return bind && bind->bound;
}

std::string_view StCore::acsi_path(AcsiTarget target) const noexcept {
    const auto img = cfg_.acsi_img.find(target);
    return img ? img->view() : std::string_view{};
}

}
