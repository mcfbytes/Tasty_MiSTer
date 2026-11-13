// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/n64_load_facts.h"

#include "cores/save_rows.h"

namespace mister::cores::n64 {

static_assert(
    proto::BitField{proto::StatusBit{32}, 1}.read(proto::StatusWord{}) == proto::FieldValue{}
);

void LoadFacts::set_field_(proto::BitField f, proto::FieldValue v) noexcept {
    edit = edit.then(proto::StatusEdit::field(f, v));
}

void LoadFacts::set_flag(proto::BitField f, bool on) noexcept {
    set_field_(f, proto::FieldValue{on ? 1u : 0u});
}

void LoadFacts::set_system(SystemType t) noexcept {
    set_field_(opt::kSysType, field_of(t));
}

void LoadFacts::set_cic(Cic c) noexcept {
    set_field_(opt::kCicType, field_of(c));
}

void LoadFacts::set_controller(PadPort port, PadType p) noexcept {
    set_field_(field_of(port), field_of(p));
}

proto::StatusWord LoadFacts::merged(proto::StatusWord fresh) const noexcept {
    return edit.over(fresh);
}

SaveRows LoadFacts::save_plan(proto::StatusWord fresh) const noexcept {
    const Options opts{merged(fresh)};
    return plan_saves(opts.cart_save_type(), opts.tpak(), opts.cpak());
}

void LoadFacts::set_cart_save_type(MemoryType t) noexcept {
    set_field_(opt::kSaveType, save_size(t) != 0 ? field_of(t) : field_of(MemoryType::None));
}

}
