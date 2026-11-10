// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/n64_load_facts.h"
#include "cores/save_rows.h"

namespace mister::cores::n64 {

static_assert(StatusField{32, 1}.read(proto::StatusWord{}) == proto::FieldValue{});

proto::FieldValue LoadFacts::field_(StatusField f) const noexcept {
    return f.read(bits);
}

void LoadFacts::set_field_(StatusField f, std::uint32_t v) noexcept {
    bits = f.written(bits, proto::FieldValue{v});
    mask = f.written(mask, f.all());
}

bool LoadFacts::flag(StatusField f) const noexcept {
    return field_(f) != proto::FieldValue{};
}

void LoadFacts::set_flag(StatusField f, bool on) noexcept {
    set_field_(f, on ? 1u : 0u);
}

SystemType LoadFacts::system() const noexcept {
    return static_cast<SystemType>(field_(opt::kSysType).v);
}

void LoadFacts::set_system(SystemType t) noexcept {
    set_field_(opt::kSysType, static_cast<std::uint32_t>(t));
}

Cic LoadFacts::cic() const noexcept {
    return static_cast<Cic>(field_(opt::kCicType).v);
}

void LoadFacts::set_cic(Cic c) noexcept {
    set_field_(opt::kCicType, static_cast<std::uint32_t>(c));
}

PadType LoadFacts::controller(StatusField f) const noexcept {
    return static_cast<PadType>(field_(f).v);
}

void LoadFacts::set_controller(StatusField f, PadType p) noexcept {
    set_field_(f, static_cast<std::uint32_t>(p));
}

bool LoadFacts::auto_detect() const noexcept {

    return !flag(opt::kAutoDetect);
}

bool LoadFacts::auto_pak() const noexcept {
    return !flag(opt::kAutoPak);
}

MemoryType LoadFacts::cart_save_type() const noexcept {
    return save_type_of(field_(opt::kSaveType));
}

proto::StatusWord LoadFacts::merged(proto::StatusWord fresh) const noexcept {
    for (unsigned b = 0; b < proto::StatusWord::kBits; ++b) {
        const proto::StatusBit bit{static_cast<std::uint8_t>(b)};
        if (mask.has(bit))
            fresh = fresh.with(bit, bits.has(bit));
    }
    return fresh;
}

SaveRows LoadFacts::save_plan(proto::StatusWord fresh) const noexcept {
    const proto::StatusWord m = merged(fresh);
    return plan_saves(
        save_type_of(opt::kSaveType.read(m)),
        opt::kTpak.read(m) != proto::FieldValue{},
        opt::kCpak.read(m) != proto::FieldValue{}
    );
}

void LoadFacts::set_cart_save_type(MemoryType t) noexcept {
    const std::uint32_t v = save_size(t) != 0 ? static_cast<std::uint32_t>(t) : 0u;
    set_field_(opt::kSaveType, v);
}

}
