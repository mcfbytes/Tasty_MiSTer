// SPDX-License-Identifier: GPL-3.0-or-later
#include "proto/savestate_engine.h"

#include <utility>

namespace mister::proto {

namespace {

enum : std::size_t { kPathSlack = 8 };

enum : std::uint64_t {
    kHeaderWords = 2,
    kDwordBytes = 4,
};

constexpr infra::IndexedArray<const char*, SaveStateEngine::kSlots, StateSlot> make_slot_names() {
    infra::IndexedArray<const char*, SaveStateEngine::kSlots, StateSlot> names{};
    constexpr const char* kText[] = {"savestate-0", "savestate-1", "savestate-2", "savestate-3"};
    std::size_t i = 0;
    for (const StateSlot slot : kStateSlots)
        names[slot] = kText[i++];
    return names;
}

constexpr auto kSlotNames = make_slot_names();

bool exceeds_four_slot_span(const SaveStateDecl& ss, hal::PhysRegion aperture) noexcept {
    const std::uint64_t span = hal::to_phys(ss.base).v
        + std::uint64_t{ss.size} * SaveStateEngine::kSlots;
    const std::uint64_t end = aperture.phys.v + std::uint64_t{aperture.len};
    return span >= end;
}

std::string compose_path(std::string_view subdir, std::string_view stem, unsigned digit) {
    std::string out;
    out.reserve(SaveStateEngine::kDir.size() + subdir.size() + stem.size() + kPathSlack);
    out.append(SaveStateEngine::kDir);
    out.push_back('/');
    out.append(subdir);
    out.push_back('/');
    out.append(stem);
    if (digit != 0u) {
        out.push_back('_');
        out.push_back(static_cast<char>('0' + static_cast<int>(digit)));
    }
    out.append(SaveStateEngine::kExt);
    return out;
}

}

std::string SaveStateEngine::slot_file(
    std::string_view subdir,
    std::string_view stem,
    unsigned digit
) {
    return compose_path(subdir, stem, digit);
}

bool SaveStateEngine::window_valid(const SaveStateDecl& ss) const noexcept {
    if (ss.size == 0u || ss.size > kMaxSlotBytes)
        return false;
    const std::uint64_t low = aperture_.phys.v;
    const std::uint64_t end = aperture_.phys.v + std::uint64_t{aperture_.len};
    const std::uint64_t base = hal::to_phys(ss.base).v;
    if (base < low || base >= end)
        return false;
    const std::uint64_t ss_end = base + std::uint64_t{ss.size};
    return ss_end < end;
}

std::string SaveStateEngine::savestate_dir(const SaveTarget& target) {
    const std::string_view subdir = target.arcade ? kArcadeSubdir : target.core_name;
    std::string out;
    out.reserve(kDir.size() + subdir.size() + 1u);
    out.append(kDir);
    out.push_back('/');
    out.append(subdir);
    return out;
}

std::string SaveStateEngine::savestate_stem(std::string_view rom_path) {
    const std::size_t slash = rom_path.find_last_of('/');
    std::string_view base = (slash == std::string_view::npos) ? rom_path
                                                              : rom_path.substr(slash + 1u);
    const std::size_t dot = base.find_last_of('.');
    if (dot != std::string_view::npos)
        base = base.substr(0, dot);
    return std::string(base);
}

std::string SaveStateEngine::savestate_path(const SaveTarget& target, StateSlot slot) {
    if (!slot.valid()) {
        fatal(Error{Errc::slot_range, ERR_SITE(), slot.digit()}, "savestate slot");
    }
    const std::string_view subdir = target.arcade ? kArcadeSubdir : target.core_name;
    return compose_path(subdir, savestate_stem(target.rom_path), slot.digit());
}

std::string SaveStateEngine::savestate_path_stock(const SaveTarget& target) {
    const std::string_view subdir = target.arcade ? kArcadeSubdir : target.core_name;
    return compose_path(subdir, savestate_stem(target.rom_path), 0u);
}

Ex<void> SaveStateEngine::arm(const SaveTarget& target) {
    const std::string_view subdir = target.arcade ? kArcadeSubdir : target.core_name;
    std::string stem = savestate_stem(target.rom_path);

    if (subdir.empty() || stem.empty()) {
        return std::unexpected(Error{Errc::negotiation, ERR_SITE(), 0});
    }
    subdir_.assign(subdir);
    stem_ = std::move(stem);
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
    return compose_path(subdir_, stem_, slot.digit());
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

    const auto now = env_.clock.now();
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
