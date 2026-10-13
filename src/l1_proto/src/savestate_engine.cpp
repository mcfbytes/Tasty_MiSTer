// SPDX-License-Identifier: GPL-3.0-or-later
#include "proto/savestate_engine.h"

#include <utility>

namespace mister::proto {

namespace {

constexpr const char* kSlotNames[SaveStateEngine::kSlots] = {
    "savestate-0",
    "savestate-1",
    "savestate-2",
    "savestate-3",
};

bool exceeds_four_slot_span(const SaveStateDecl& ss, hal::PhysRegion aperture) noexcept {
    const std::uint64_t span =
        hal::to_phys(ss.base).v + std::uint64_t{ss.size} * SaveStateEngine::kSlots;
    const std::uint64_t end = aperture.phys.v + std::uint64_t{aperture.len};
    return span >= end;
}

std::string compose_path(std::string_view subdir, std::string_view stem, unsigned digit) {
    std::string out;
    out.reserve(SaveStateEngine::kDir.size() + subdir.size() + stem.size() + 8u);
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

}  // namespace

std::string SaveStateEngine::slot_file(std::string_view subdir, std::string_view stem,
                                       unsigned digit) {
    return compose_path(subdir, stem, digit);
}

bool SaveStateEngine::window_valid(const SaveStateDecl& ss) const noexcept {
    if (ss.size == 0u || ss.size > kMaxSlotBytes) return false;
    const std::uint64_t low = aperture_.phys.v;
    const std::uint64_t end = aperture_.phys.v + std::uint64_t{aperture_.len};
    const std::uint64_t base = hal::to_phys(ss.base).v;
    if (base < low || base >= end) return false;
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
    std::string_view base =
        (slash == std::string_view::npos) ? rom_path : rom_path.substr(slash + 1u);
    const std::size_t dot = base.find_last_of('.');
    if (dot != std::string_view::npos) base = base.substr(0, dot);
    return std::string(base);
}

std::string SaveStateEngine::savestate_path(const SaveTarget& target, unsigned slot) {
    if (slot >= kSlots) {
        fatal(Error{Errc::slot_range, ERR_SITE(), slot}, "savestate slot");
    }
    const std::string_view subdir = target.arcade ? kArcadeSubdir : target.core_name;
    return compose_path(subdir, savestate_stem(target.rom_path), slot + 1u);
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

void SaveStateEngine::disarm() noexcept { armed_ = false; }

std::uint32_t SaveStateEngine::shadow_counter(unsigned slot) const {
    if (slot >= kSlots) {
        fatal(Error{Errc::slot_range, ERR_SITE(), slot}, "savestate slot");
    }
    return ss_cnt_[slot];
}

std::string SaveStateEngine::path_for(unsigned slot) const {
    return compose_path(subdir_, stem_, slot + 1u);
}

hal::PhysRegion SaveStateEngine::region_for(const SaveStateDecl& ss, unsigned slot) {

    const hal::FabricAddr at{ss.base.v + static_cast<std::uint32_t>(slot) * ss.size};
    return hal::PhysRegion{hal::to_phys(at), static_cast<std::size_t>(ss.size), kSlotNames[slot]};
}

Ex<void> SaveStateEngine::map_slots(const SaveStateDecl& ss, bool remap) {
    for (unsigned i = 0; i < kSlots; ++i) {
        if (remap) regions_[i].reset();
        auto m = env_.map.map(region_for(ss, i));
        if (!m) {

            drop_slots();
            return std::unexpected(m.error());
        }
        regions_[i] = std::move(*m);
    }
    return {};
}

void SaveStateEngine::drop_slots() noexcept {
    for (unsigned i = 0; i < kSlots; ++i) {
        regions_[i].reset();
        ss_cnt_[i] = kSentinel;
    }
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

    if (exceeds_four_slot_span(ss, aperture_)) ++span_warnings_;

    if (auto m = map_slots(ss, mapped_); !m) return m;
    window_ = ss;
    mapped_ = true;
    for (auto& c : ss_cnt_)
        c = kSentinel;
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

    if (exceeds_four_slot_span(ss, aperture_)) ++span_warnings_;
    if (auto m = map_slots(ss, mapped_); !m) return m;
    window_ = ss;
    mapped_ = true;
    for (auto& c : ss_cnt_)
        c = kSentinel;
    return {};
}

Ex<void> SaveStateEngine::scan_slots() {
    std::optional<Error> first_error;

    for (unsigned i = 0; i < kSlots; ++i) {
        auto& mem = *regions_[i];

        const std::uint32_t curcnt = mem.load_acquire(kCounterWord);
        const std::uint32_t dwords = mem.load_acquire(kSizeWord);
        if (curcnt == ss_cnt_[i]) continue;

        ss_cnt_[i] = curcnt;
        if (dwords == 0u) continue;

        const std::uint64_t bytes = (std::uint64_t{dwords} + 2u) * 4u;
        if (bytes > std::uint64_t{window_.size}) continue;

        if (env_.toast) env_.toast->on_save(i);

        const auto payload = mem.view(0, static_cast<std::size_t>(bytes));
        auto w = env_.files.write_all_sync(path_for(i), payload);
        if (!w) {

            ++write_failures_;
            if (!first_error) first_error = w.error();
            continue;
        }
        ++saves_written_;
    }

    if (first_error) return std::unexpected(*first_error);
    return {};
}

Ex<void> SaveStateEngine::poll() {
    if (!mapped_ || !armed_) return {};

    const auto now = env_.clock.now();
    if (next_scan_ && now < *next_scan_) return {};
    next_scan_ = now + kScanPeriod;

    return scan_slots();
}

Ex<void> SaveStateEngine::detach() {
    Ex<void> result{};

    if (mapped_ && armed_) result = scan_slots();

    drop_slots();
    armed_ = false;
    window_ = SaveStateDecl{};
    next_scan_.reset();
    return result;
}

}  // namespace mister::proto
