// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <type_traits>

#include "cores/save_rows.h"
#include "hal/types.h"
#include "infra/endian.h"
#include "infra/seat.h"

#include "cores/n64_memory_type.h"
#include "cores/n64_option_view.h"
#include "cores/n64_options.h"
#include "cores/n64_system_type.h"
#include "proto/status_edit.h"
#include "proto/status_word.h"
#include "proto/types.h"
#include "svc/types.h"

namespace mister::cores::n64 {

enum class FactsKind : std::uint8_t { Rom, IplBoot, IplPreload, Disk };

struct LoadFacts {
    TASTY_SEAT_EXEMPT(component);
    proto::StatusEdit edit{};
    svc::Crc32 crc32{};
    proto::FileSize bytes{};
    hal::FabricAddr load_addr{};
    FactsKind kind = FactsKind::Rom;
    std::uint8_t pad[3]{};

    SaveRows saves{};

    [[nodiscard]] static std::optional<LoadFacts> of(std::span<const std::uint8_t> bytes) noexcept {
        if (bytes.size() != sizeof(LoadFacts))
            return std::nullopt;
        const auto f = infra::load_native<LoadFacts>(bytes.first<sizeof(LoadFacts)>());
        if (f.kind > FactsKind::Disk || !f.saves.count.fits())
            return std::nullopt;
        return f;
    }

    [[nodiscard]] Options options() const noexcept { return Options{edit.bits}; }

    void set_flag(proto::BitField f, bool on) noexcept;

    void set_system(SystemType t) noexcept;
    void set_cic(Cic c) noexcept;

    void set_controller(PadPort port, PadType p) noexcept;
    void set_cart_save_type(MemoryType t) noexcept;

    [[nodiscard]] SaveRows save_plan(proto::StatusWord fresh) const noexcept;

private:
    [[nodiscard]] proto::StatusWord merged(proto::StatusWord fresh) const noexcept;

    void set_field_(proto::BitField f, proto::FieldValue v) noexcept;
};

static_assert(std::is_trivially_copyable_v<LoadFacts> && sizeof(LoadFacts) == 72);

}
