// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "cores/n64_memory_type.h"
#include "cores/n64_options.h"
#include "cores/n64_system_type.h"
#include "proto/bit_field.h"
#include "proto/status_word.h"
#include "proto/types.h"

namespace mister::cores::n64 {

class Options {
public:
    constexpr explicit Options(const proto::StatusWord& word) noexcept : word_(word) {}

    [[nodiscard]] constexpr proto::FieldValue field(proto::BitField f) const noexcept {
        return f.read(word_);
    }

    [[nodiscard]] constexpr bool flag(proto::BitField f) const noexcept {
        return field(f) != proto::FieldValue{};
    }

    [[nodiscard]] constexpr SystemType system() const noexcept {
        return system_type_of(field(opt::kSysType));
    }

    [[nodiscard]] constexpr Cic cic() const noexcept { return cic_of(field(opt::kCicType)); }

    [[nodiscard]] constexpr PadType controller(PadPort port) const noexcept {
        return pad_type_of(field(field_of(port)));
    }

    [[nodiscard]] constexpr MemoryType cart_save_type() const noexcept {
        return save_type_of(field(opt::kSaveType));
    }

    [[nodiscard]] constexpr bool auto_detect() const noexcept { return !flag(opt::kAutoDetect); }

    [[nodiscard]] constexpr bool auto_pak() const noexcept { return !flag(opt::kAutoPak); }

    [[nodiscard]] constexpr bool cheats_enabled() const noexcept { return !flag(opt::kCheats); }

    [[nodiscard]] constexpr bool autosave() const noexcept { return !flag(opt::kAutosave); }

    [[nodiscard]] constexpr bool tpak() const noexcept { return flag(opt::kTpak); }

    [[nodiscard]] constexpr bool cpak() const noexcept { return flag(opt::kCpak); }

    [[nodiscard]] constexpr bool patches_auto() const noexcept { return !flag(opt::kPatches); }

private:
    proto::StatusWord word_;
};

}
