// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/input_service.h"
#include "hal/selected.h"

#include "input_rules.h"

namespace mister::svc {

namespace {

constexpr hal::SpiWord kGetKbdLed{0x1F};
constexpr hal::SpiWord kPs2Ctl{0x21};
constexpr std::uint16_t kLedPollTicks = 100;

}

namespace {

std::uint32_t key_hash(std::string_view key) noexcept {
    if (key.empty())
        return 0u;
    std::uint32_t h = 0x811c9dc5u;
    for (const char c : key) {
        h ^= static_cast<std::uint8_t>(c);
        h *= 0x01000193u;
    }
    return h == 0u ? 1u : h;
}
}

void InputService::refresh_slot_census() noexcept {
    std::uint32_t bound = 0;
    std::uint32_t live = 0;
    for (const InputDevice& d : table_.devices) {
        if (const auto s = players_.slot_of(d.identity())) {
            ++bound;
            live |= 1u << s->v;
        }
    }
    counters_.set_bound(bound, live);

    std::uint32_t ghosts = 0;
    for (std::uint8_t i = 0; i < kMaxPlayers; ++i) {
        const std::uint32_t h = key_hash(players_.key_at(i));
        counters_.set_slot_hash(i, h);
        if (h != 0u && (live & (1u << i)) == 0u)
            ++ghosts;
    }
    counters_.set_ghosts(ghosts);
}

InputService InputService::create() {
    return InputService();
}

void InputService::install(DeviceTable& incoming) noexcept {
    std::swap(table_, incoming);
    for (InputDevice& d : incoming.devices) {
        if (!d.release_for_handover())
            counters_.note_grab_failure();
    }
    for (InputDevice& d : table_.devices) {
        if (grabbed_ && !d.set_grabbed(true))
            counters_.note_grab_failure();
    }
    ++devices_gen_;
    for (JoyMask& m : joy_mask_) {
        m = JoyMask{};
    }
    counters_.set_mapped(table_.mapped);
    refresh_slot_census();
    for (InputDevice& d : table_.devices) {
        if (d.drain())
            note_report(d);
    }
}

void InputService::apply_deadzone_rules(std::span<const DeadzoneRule> rows) noexcept {
    for (InputDevice& d : table_.devices) {
        const DeviceIdentity& id = d.identity();
        d.set_deadzone(
            rules::deadzone_for(d.quirk(), id.vid, id.pid, id.id, d.sysfs(), id.merge_id, rows)
        );
    }
}

std::uint8_t InputService::mouse_button_level() const noexcept {
    std::uint8_t level = 0;
    for (const InputDevice& d : table_.devices) {
        if (!d.is_mouse())
            continue;
        level = static_cast<std::uint8_t>(level | d.report().mouse_buttons);
    }
    return level;
}

Ex<unsigned> InputService::on_fd_ready(int fd) {
    if (fd < 0)
        return std::unexpected(Error{Errc::not_found, ERR_SITE(), 0});
    if (hotplug_.valid() && fd == hotplug_.fd()) {
        if (auto r = hotplug_.on_ready(); !r)
            return std::unexpected(r.error());
        return 0u;
    }
    for (InputDevice& d : table_.devices) {
        if (d.fd() != fd)
            continue;
        auto n = d.drain();
        if (!n)
            return std::unexpected(n.error());
        note_report(d);
        return *n;
    }
    return std::unexpected(Error{Errc::not_found, ERR_SITE(), static_cast<std::uint32_t>(fd)});
}

void InputService::note_report(InputDevice& dev) {
    const DeviceReport& r = dev.report();

    counters_.note_drain(r);

    maybe_assign_slot(dev);

    if (r.buttons_changed)
        recompute_joy_masks();
}

void InputService::maybe_assign_slot(InputDevice& dev) {

    if (dev.is_mouse())
        return;
    if (players_.slot_of(dev.identity()))
        return;
    const DeviceReport& r = dev.report();
    bool claim = false;
    for (std::uint8_t i = 0; i < r.key_count && !claim; ++i) {
        const KeyEdge& e = r.keys[i];
        if (!e.pressed || e.code < 256)
            continue;
        claim = dev.matches_sys_button(e.code);
    }
    if (!claim)
        return;

    std::uint32_t live = 0;
    for (const InputDevice& d : table_.devices) {
        if (const auto s = players_.slot_of(d.identity()))
            live |= 1u << s->v;
    }
    const auto slot = players_.first_free(live);
    if (!slot)
        return;
    if (!players_.assign(dev.identity(), *slot))
        return;
    refresh_slot_census();
}

void InputService::recompute_joy_masks() {
    counters_.note_mask_recompute();
    for (JoyMask& m : joy_mask_)
        m = JoyMask{};
    for (const InputDevice& d : table_.devices) {

        if (d.is_mouse())
            continue;
        const auto slot = players_.slot_of(d.identity());
        if (!slot)
            continue;
        joy_mask_[slot->v].v |= d.report().buttons.v;
    }
}

RumblePadSet InputService::rumble_pads() const noexcept {
    TASTY_SEAT_BODY(InputService);
    RumblePadSet out{};
    if (!cfg_rumble_)
        return out;
    for (const InputDevice& d : table_.devices) {
        if (!d.has_rumble() || d.is_mouse())
            continue;
        const auto s = players_.slot_of(d.identity());
        if (!s)
            continue;
        out.players = static_cast<std::uint8_t>(out.players | (1u << s->v));

        std::uint32_t h = out.pads[s->v] != 0 ? out.pads[s->v] : 2166136261u;
        for (const char c : PlayerSlots::key_of(d.identity())) {
            h ^= static_cast<std::uint8_t>(c);
            h *= 16777619u;
        }
        h ^= 0xFFu;
        h *= 16777619u;
        out.pads[s->v] = h != 0 ? h : 1u;
    }
    return out;
}

RumbleApply InputService::rumble(
    PlayerIndex player,
    std::uint16_t strong,
    std::uint16_t weak
) noexcept {
    TASTY_SEAT_BODY(InputService);
    if (!cfg_rumble_)
        return RumbleApply::NoDevice;
    RumbleApply out = RumbleApply::NoDevice;
    for (InputDevice& d : table_.devices) {
        if (!d.has_rumble() || d.is_mouse())
            continue;
        const auto s = players_.slot_of(d.identity());
        if (!s || s->v != player.v)
            continue;
        if (!d.set_rumble(strong, weak)) {
            out = RumbleApply::Failed;
        } else if (out == RumbleApply::NoDevice) {
            out = RumbleApply::Applied;
        }
    }
    return out;
}

JoyMask InputService::joy_mask(PlayerIndex player) const {
    if (player.v >= kMaxPlayers)
        return JoyMask{};
    return joy_mask_[player.v];
}

Ex<void> InputService::set_grabbed(bool on) {
    grabbed_ = on;
    Ex<void> first{};
    for (InputDevice& d : table_.devices) {

        if (auto r = d.set_grabbed(grabbed_); !r && first) {
            first = std::unexpected(r.error());
        }
    }
    return first;
}

Ex<void> InputService::adopt(InputDevice&& dev) {
    if (table_.devices.size() >= kMaxDevices) {
        return std::unexpected(
            Error{Errc::slot_range, ERR_SITE(), static_cast<std::uint32_t>(table_.devices.size())}
        );
    }
    table_.devices.push_back(std::move(dev));
    ++devices_gen_;
    return {};
}

[[nodiscard]] Ex<void> emit_but_sw(hal::ISpiTransport& link, std::uint16_t map) {
    hal::Selected cs(link, hal::ChipSelect::Io);

    if (auto r = link.transfer(hal::SpiWord{kUioButSw}); !r) {
        return std::unexpected(r.error());
    }
    if (auto r = link.transfer(hal::SpiWord{map}); !r) {
        return std::unexpected(r.error());
    }
    return {};
}

}
