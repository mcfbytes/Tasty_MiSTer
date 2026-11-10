// SPDX-License-Identifier: GPL-3.0-or-later
#include "proto/savestate_engine.h"

#include <array>
#include <utility>

namespace mister::proto {

namespace {

enum : std::uint64_t {
    kHeaderWords = 2,
    kDwordBytes = 4,
};

struct SlotNameRow {
    StateSlot slot;
    const char* name;
};

constexpr auto kSlotNameRows = std::to_array<SlotNameRow>({
    {StateSlot{0}, "savestate-0"},
    {StateSlot{1}, "savestate-1"},
    {StateSlot{2}, "savestate-2"},
    {StateSlot{3}, "savestate-3"},
});
static_assert(kSlotNameRows.size() == SaveStateEngine::kSlots, "one name per state slot");

constexpr infra::IndexedArray<const char*, SaveStateEngine::kSlots, StateSlot> make_slot_names() {
    infra::IndexedArray<const char*, SaveStateEngine::kSlots, StateSlot> names{};
    for (const SlotNameRow& row : kSlotNameRows)
        names[row.slot] = row.name;
    return names;
}

constexpr auto kSlotNames = make_slot_names();

bool exceeds_four_slot_span(const SaveStateDecl& ss, hal::PhysRegion aperture) noexcept {
    const std::uint64_t span = hal::to_phys(ss.base).v
        + std::uint64_t{ss.size} * SaveStateEngine::kSlots;
    const std::uint64_t end = aperture.phys.v + std::uint64_t{aperture.len};
    return span >= end;
}

}

bool SaveStateEngine::window_valid(const SaveStateDecl& ss) const noexcept {
    return ss.within(aperture_);
}

Ex<void> SaveStateEngine::arm(const SaveTarget& target) {

    std::optional<SaveStateName> name = SaveStateName::of(target);
    if (!name) {
        return std::unexpected(Error{Errc::negotiation, ERR_SITE(), 0});
    }
    files_ = std::move(*name);
    armed_ = true;
    return {};
}

void SaveStateEngine::disarm() noexcept {
    armed_ = false;
}

std::uint32_t SaveStateEngine::shadow_counter(StateSlot slot) const {
    const auto shadow = shadows_.find(slot);
    if (!shadow) {
        fatal(Error{Errc::slot_range, ERR_SITE(), slot.digit()}, "savestate slot");
    }
    return shadow->counter;
}

std::string SaveStateEngine::path_for(StateSlot slot) const {
    return files_.path(slot);
}

hal::PhysRegion SaveStateEngine::region_for(const SaveStateDecl& ss, StateSlot slot) {

    const hal::FabricAddr at{ss.base.v + slot.offset(ss.size)};
    return hal::PhysRegion{hal::to_phys(at), static_cast<std::size_t>(ss.size), kSlotNames[slot]};
}

Ex<void> SaveStateEngine::map_slots(const SaveStateDecl& ss, bool remap) {
    for (const StateSlot slot : kStateSlots) {
        if (remap)
            shadows_[slot].region.reset();
        auto m = env_.map.map(region_for(ss, slot));
        if (!m) {

            drop_slots();
            return std::unexpected(m.error());
        }
        shadows_[slot].region = std::move(*m);
    }
    return {};
}

void SaveStateEngine::drop_slots() noexcept {
    for (auto& shadow : shadows_)
        shadow.drop();
    mapped_ = false;

    window_ = SaveStateDecl{};
}

Ex<void> SaveStateEngine::attach(const SaveStateDecl& ss) {
    if (!armed_) {

        return std::unexpected(Error{Errc::negotiation, ERR_SITE(), 1});
    }
    if (!window_valid(ss)) {
        return std::unexpected(Error{Errc::bad_confstr, ERR_SITE(), ss.size});
    }
    if (ss.size < kHeaderBytes) {

        return std::unexpected(Error{Errc::bad_confstr, ERR_SITE(), ss.size});
    }

    if (exceeds_four_slot_span(ss, aperture_))
        ++span_warnings_;

    if (auto m = map_slots(ss, mapped_); !m)
        return m;
    window_ = ss;
    mapped_ = true;
    for (auto& shadow : shadows_)
        shadow.rearm();
    ever_attached_ = true;
    return {};
}

Ex<void> SaveStateEngine::rebind(const SaveStateDecl& ss) {
    if (!ever_attached_) {
        return std::unexpected(Error{Errc::negotiation, ERR_SITE(), 2});
    }
    if (!window_valid(ss) || ss.size < kHeaderBytes) {
        return std::unexpected(Error{Errc::bad_confstr, ERR_SITE(), ss.size});
    }

    if (exceeds_four_slot_span(ss, aperture_))
        ++span_warnings_;
    if (auto m = map_slots(ss, mapped_); !m)
        return m;
    window_ = ss;
    mapped_ = true;
    for (auto& shadow : shadows_)
        shadow.rearm();
    return {};
}

Ex<void> SaveStateEngine::scan_slots() {
    std::optional<Error> first_error;

    for (const StateSlot slot : kStateSlots) {
        Shadow& shadow = shadows_[slot];
        auto& mem = *shadow.region;

        const std::uint32_t curcnt = mem.load_acquire(kCounterWord);
        const std::uint32_t dwords = mem.load_acquire(kSizeWord);
        if (curcnt == shadow.counter)
            continue;

        shadow.counter = curcnt;
        if (dwords == 0u)
            continue;

        const std::uint64_t bytes = (std::uint64_t{dwords} + kHeaderWords) * kDwordBytes;
        if (bytes > std::uint64_t{window_.size})
            continue;

        if (env_.toast)
            env_.toast->on_save(slot);

        const auto payload = mem.view(0, static_cast<std::size_t>(bytes));
        auto w = env_.files.write_all_sync(path_for(slot), payload);
        if (!w) {

            ++write_failures_;
            if (!first_error)
                first_error = w.error();
            continue;
        }
        ++saves_written_;
    }

    if (first_error)
        return std::unexpected(*first_error);
    return {};
}

Ex<void> SaveStateEngine::poll() {
    if (!mapped_ || !armed_)
        return {};

    const std::chrono::nanoseconds now = env_.clock.now().time_since_epoch();
    if (next_scan_ && now < *next_scan_)
        return {};
    next_scan_ = now + kScanPeriod;

    return scan_slots();
}

Ex<void> SaveStateEngine::detach() {
    Ex<void> result{};

    if (mapped_ && armed_)
        result = scan_slots();

    drop_slots();
    armed_ = false;
    window_ = SaveStateDecl{};
    next_scan_.reset();
    return result;
}

}
