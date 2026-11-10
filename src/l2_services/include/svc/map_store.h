// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "infra/error.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "svc/packed_keys.h"

namespace mister::svc {

struct DeviceIdentity;
struct ButtonMap;

class Vfs;

enum class MapKind : std::uint8_t {
    Joystick,
    Advanced,
    Keyboard,
    Gun,
    GunCal,
    kCount,
};

enum class StemSource : std::uint8_t { Id, VidPid };

enum class PathPolicy : std::uint8_t { InputsThenConfig, InputsOnly, ConfigOnly };

struct MapKindSpec {
    MapKind kind;
    std::size_t bytes;
    bool core_prefix;
    std::string_view infix;
    StemSource stem;
    bool mod_suffix;
    std::string_view suffix;
    PathPolicy paths;
};

inline constexpr auto kMapKinds = std::to_array<MapKindSpec>({
    {.kind = MapKind::Joystick,
     .bytes = 128,
     .core_prefix = true,
     .infix = "input_",
     .stem = StemSource::Id,
     .mod_suffix = true,
     .suffix = "_v3.map",
     .paths = PathPolicy::InputsThenConfig},
    {.kind = MapKind::Advanced,
     .bytes = 768,
     .core_prefix = true,
     .infix = "advanced_input_",
     .stem = StemSource::Id,
     .mod_suffix = true,
     .suffix = "_v1.map",
     .paths = PathPolicy::InputsOnly},
    {.kind = MapKind::Keyboard,
     .bytes = 256,
     .core_prefix = false,
     .infix = "kbd_",
     .stem = StemSource::Id,
     .mod_suffix = false,
     .suffix = ".map",
     .paths = PathPolicy::ConfigOnly},
    {.kind = MapKind::Gun,
     .bytes = 2048,
     .core_prefix = true,
     .infix = "input_",
     .stem = StemSource::Id,
     .mod_suffix = false,
     .suffix = "_jk.map",
     .paths = PathPolicy::InputsThenConfig},
    {.kind = MapKind::GunCal,
     .bytes = 16,
     .core_prefix = true,
     .infix = "gun_cal_",
     .stem = StemSource::VidPid,
     .mod_suffix = false,
     .suffix = "_v2.cfg",
     .paths = PathPolicy::ConfigOnly},
});

static_assert(
    std::size(kMapKinds) == static_cast<std::size_t>(MapKind::kCount),
    "every MapKind needs exactly one kMapKinds row"
);

consteval bool map_kinds_are_indexed_by_enumerator() {
    for (std::size_t i = 0; i < std::size(kMapKinds); ++i) {
        if (kMapKinds[i].kind != static_cast<MapKind>(i))
            return false;
    }
    return true;
}

static_assert(map_kinds_are_indexed_by_enumerator());

constexpr const MapKindSpec& map_spec(MapKind k) noexcept {
    return kMapKinds[static_cast<std::size_t>(k)];
}

class MapStore {

public:
    MapStore() noexcept = default;

    explicit MapStore(const Vfs& vfs) noexcept : vfs_(vfs) {}

    static std::string filename(
        std::string_view core,
        const DeviceIdentity& id,
        MapKind kind,
        bool mod
    );

    infra::OptRef<const Vfs> storage() const noexcept { return vfs_; }

    [[nodiscard]] Ex<ButtonMap> load(
        std::string_view core,
        const DeviceIdentity& id,
        MapKind kind,
        bool mod
    ) const;

    [[nodiscard]] Ex<WireWords> load_joy_words(
        std::string_view core,
        const DeviceIdentity& id,
        bool mod
    ) const;
    [[nodiscard]] Ex<void> save(
        std::string_view core,
        const DeviceIdentity& id,
        const ButtonMap& map,
        bool mod
    ) const;

private:
    infra::OptRef<const Vfs> vfs_;
};

}
