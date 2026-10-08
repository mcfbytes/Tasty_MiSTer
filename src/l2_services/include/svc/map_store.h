// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "infra/error.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"

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

inline constexpr MapKindSpec kMapKinds[] = {

    {MapKind::Joystick, 128, true, "input_", StemSource::Id, true, "_v3.map",
     PathPolicy::InputsThenConfig},
    {MapKind::Advanced, 768, true, "advanced_input_", StemSource::Id, true, "_v1.map",
     PathPolicy::InputsOnly},
    {MapKind::Keyboard, 256, false, "kbd_", StemSource::Id, false, ".map", PathPolicy::ConfigOnly},
    {MapKind::Gun, 2048, true, "input_", StemSource::Id, false, "_jk.map",
     PathPolicy::InputsThenConfig},
    {MapKind::GunCal, 16, true, "gun_cal_", StemSource::VidPid, false, "_v2.cfg",
     PathPolicy::ConfigOnly},
};

static_assert(std::size(kMapKinds) == static_cast<std::size_t>(MapKind::kCount),
              "every MapKind needs exactly one kMapKinds row");
consteval bool map_kinds_are_indexed_by_enumerator() {
    for (std::size_t i = 0; i < std::size(kMapKinds); ++i) {
        if (kMapKinds[i].kind != static_cast<MapKind>(i)) return false;
    }
    return true;
}
static_assert(map_kinds_are_indexed_by_enumerator());

constexpr const MapKindSpec& map_spec(MapKind k) noexcept {
    return kMapKinds[static_cast<std::size_t>(k)];
}

class MapStore {
    TASTY_SEAT_EXEMPT(component);

public:
    MapStore() noexcept = default;

    explicit MapStore(const Vfs& vfs) noexcept : vfs_(vfs) {}

    static std::string filename(std::string_view core, const DeviceIdentity& id, MapKind kind,
                                bool mod);

    infra::OptRef<const Vfs> storage() const noexcept { return vfs_; }

    Ex<ButtonMap> load(std::string_view core, const DeviceIdentity& id, MapKind kind,
                       bool mod) const;
    Ex<void> save(std::string_view core, const DeviceIdentity& id, const ButtonMap& map,
                  bool mod) const;

private:
    infra::OptRef<const Vfs> vfs_;
};

}  // namespace mister::svc
