// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/input/input_service.h"

#include <utility>

#include "infra/prelude.h"
#include "input_rules.h"

namespace mister::svc::input {

std::uint32_t fnv1a_32(std::string_view bytes, std::uint32_t seed) noexcept {
    std::uint32_t h = seed;
    for (const char c : bytes) {
        h ^= static_cast<std::uint8_t>(c);
        h *= kFnvPrime;
    }
    return h;
}

namespace {

KeyHash key_hash(std::string_view key) noexcept {
    if (key.empty())
        return KeyHash{};
    const std::uint32_t h = fnv1a_32(key);
    return KeyHash{h == 0u ? 1u : h};
}

constexpr std::uint32_t kPadBoundaryByte = 0xFFu;

}

PadSignature PadSignature::of(std::string_view key) noexcept {
    return none().folded(key);
}

PadSignature PadSignature::folded(std::string_view next_key) const noexcept {
    const std::uint32_t seed = is_bound() ? v : kFnvOffsetBasis;
    const std::uint32_t boundary_fold = fnv1a_32(next_key, seed) ^ kPadBoundaryByte;
    const std::uint32_t signature = boundary_fold * kFnvPrime;
    return PadSignature{signature != 0u ? signature : 1u};
}

void InputService::refresh_slot_census() noexcept {
    std::uint32_t bound = 0;
    PlayerMask live{};
    for (const InputDevice& d : table_.devices) {
        if (const auto s = players_.slot_of(d.identity())) {
            ++bound;
            live = live.with(*s);
        }
    }
    counters_.set_bound(bound, live);

    std::uint32_t ghosts = 0;
    for (const PlayerIndex player : kPlayers) {
        const KeyHash h = key_hash(players_.key_at(player));
        counters_.set_slot_hash(player, h);
        if (h.is_bound() && !live.contains(player))
            ++ghosts;
    }
    counters_.set_ghosts(ghosts);
}

Ex<InputService> InputService::open(std::string_view input_dir) {
    auto watch = HotplugWatch::open(input_dir);
    if (!watch)
        return std::unexpected(watch.error());
    return InputService{std::move(*watch)};
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
    devices_gen_ = devices_gen_.next();
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
        d.set_deadzone(rules::deadzone_for(d.facts(), rows));
    }
}

proto::MouseButtons InputService::mouse_button_level() const noexcept {
    proto::MouseButtons level{};
    for (const InputDevice& d : table_.devices) {
        if (!d.is_mouse())
            continue;
        level = level.with(d.report().mouse_buttons);
    }
    return level;
}

Ex<unsigned> InputService::on_fd_ready(int fd) {
    if (fd < 0)
        return std::unexpected(Error{Errc::not_found, ERR_SITE(), 0});
    if (fd == hotplug_.fd()) {
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
    if (!r.dials.empty())
        fold_dials(dev);
    if (r.stick_changed[kLeftStick] || r.stick_changed[kRightStick])
        fold_sticks(dev);
}

void InputService::fold_sticks(const InputDevice& dev) {

    if (dev.is_mouse())
        return;
    const auto slot = players_.slot_of(dev.identity());
    if (!slot)
        return;
    const DeviceReport& r = dev.report();
    for (const StickIndex s : kSticks)
        if (r.stick_changed[s])
            sticks_[*slot].level[s] = r.stick[s];
}

AnalogXy InputService::stick(PlayerIndex player, StickIndex which) const noexcept {
    const auto sticks = sticks_.find(player);
    if (!sticks)
        return AnalogXy{};
    const auto level = sticks->level.find(which);
    return level ? *level : AnalogXy{};
}

void InputService::fold_dials(const InputDevice& dev) {

    if (dev.is_mouse())
        return;
    const auto slot = players_.slot_of(dev.identity());
    if (!slot)
        return;
    const auto owed = dials_.find(*slot);
    if (owed)
        *owed = owed->folded(dev.report().dials);
}

DialReport InputService::take_dials(PlayerIndex player) noexcept {
    TASTY_SEAT_BODY(InputService);
    const auto owed = dials_.find(player);
    if (!owed)
        return {};
    return std::exchange(*owed, DialReport{});
}

void InputService::maybe_assign_slot(InputDevice& dev) {

    if (dev.is_mouse())
        return;
    if (players_.slot_of(dev.identity()))
        return;
    const DeviceReport& r = dev.report();
    bool claim = false;
    for (std::size_t i = 0; i < r.keys.count() && !claim; ++i) {
        const KeyEvent& e = r.keys.at(i);
        if (!e.pressed || e.code.is_key())
            continue;
        claim = dev.matches_sys_button(e.code);
    }
    if (!claim)
        return;

    PlayerMask live{};
    for (const InputDevice& d : table_.devices) {
        if (const auto s = players_.slot_of(d.identity()))
            live = live.with(*s);
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
        joy_mask_[*slot] = joy_mask_[*slot].with(d.report().buttons);
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
        out.players = out.players.with(*s);

        out.pads[*s] = out.pads[*s].folded(PlayerSlots::key_of(d.identity()));
    }
    return out;
}

RumbleApply InputService::rumble(PlayerIndex player, proto::user_io::RumbleMotors motors) noexcept {
    TASTY_SEAT_BODY(InputService);
    if (!cfg_rumble_)
        return RumbleApply::NoDevice;
    RumbleApply out = RumbleApply::NoDevice;
    for (InputDevice& d : table_.devices) {
        if (!d.has_rumble() || d.is_mouse())
            continue;
        const auto s = players_.slot_of(d.identity());
        if (!s || *s != player)
            continue;
        if (!d.set_rumble(motors)) {
            out = RumbleApply::Failed;
        } else if (out == RumbleApply::NoDevice) {
            out = RumbleApply::Applied;
        }
    }
    return out;
}

JoyMask InputService::joy_mask(PlayerIndex player) const {
    const auto mask = joy_mask_.find(player);
    return mask ? *mask : JoyMask{};
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
    if (auto r = table_.add(std::move(dev)); !r)
        return r;
    devices_gen_ = devices_gen_.next();
    return {};
}

}
