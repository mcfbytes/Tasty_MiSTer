// SPDX-License-Identifier: GPL-3.0-or-later
#include "proto/savestate_engine.h"

#include <cstring>
#include <utility>

namespace mister::proto {

namespace {

constexpr const char* kSlotNames[SaveStateEngine::kSlots] = {
    "savestate-0",
    "savestate-1",
    "savestate-2",
    "savestate-3",
};

bool word_aligned(const void* p) noexcept {
    return (reinterpret_cast<std::uintptr_t>(p) & 0x3u) == 0u;
}

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

void SaveStateEngine::bind(const Environment& env) { env_ = env; }

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

hal::PhysRegion SaveStateEngine::region_for(const SaveStateDecl& ss, unsigned slot) const {

    const hal::FabricAddr at{ss.base.v + static_cast<std::uint32_t>(slot) * ss.size};
    return hal::PhysRegion{hal::to_phys(at), static_cast<std::size_t>(ss.size), kSlotNames[slot]};
}

Ex<void> SaveStateEngine::map_slots(const SaveStateDecl& ss, bool remap) {
    const bool borrowed = !env_.host_backing.empty();
    if (borrowed) {
        const std::size_t need = static_cast<std::size_t>(ss.size) * kSlots;

        if (env_.host_backing.size() < need || (ss.size & 0x3u) != 0u ||
            !word_aligned(env_.host_backing.data())) {
            return std::unexpected(Error{Errc::mmap_failed, ERR_SITE(), ss.size});
        }
    }

    for (unsigned i = 0; i < kSlots; ++i) {
        const hal::PhysRegion r = region_for(ss, i);
        Ex<void> rc{};
        if (borrowed) {
            const auto back =
                env_.host_backing.subspan(static_cast<std::size_t>(i) * ss.size, ss.size);
            if (remap && regions_[i]) {
                rc = regions_[i]->rebind(back, r);
            } else {
                regions_[i] = hal::FpgaMemory::borrow(back, r);
            }
        } else if (remap && regions_[i]) {
            rc = regions_[i]->rebind(r);
        } else {
            auto m = hal::FpgaMemory::map(r);
            if (!m) {
                rc = std::unexpected(m.error());
            } else {
                regions_[i] = std::move(*m);
            }
        }
        if (!rc) {

            drop_slots();
            return rc;
        }
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

Ex<void> SaveStateEngine::prime_slots() {
    for (unsigned i = 0; i < kSlots; ++i) {
        ss_cnt_[i] = kSentinel;

        const auto win = regions_[i]->view(0, static_cast<std::size_t>(window_.size));

        std::memset(win.data(), 0, win.size());

        auto r = env_.files->read_all(path_for(i), win);
        if (!r && r.error().code == Errc::not_found && i == 0u) {

            r = env_.files->read_all(compose_path(subdir_, stem_, 0u), win);
        }
        if (!r && r.error().code != Errc::not_found) {

            ++load_failures_;
        }

        regions_[i]->store_release(kCounterWord, kSentinel);
    }
    return {};
}

Ex<void> SaveStateEngine::attach(const SaveStateDecl& ss) {
    if (env_.files == nullptr || env_.clock == nullptr) {
        return std::unexpected(Error{Errc::negotiation, ERR_SITE(), 0});
    }
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
    ever_attached_ = true;
    return prime_slots();
}

Ex<void> SaveStateEngine::rebind(const SaveStateDecl& ss) {
    if (env_.files == nullptr || env_.clock == nullptr) {
        return std::unexpected(Error{Errc::negotiation, ERR_SITE(), 0});
    }
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
    return prime_slots();
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

        if (env_.save_toast != nullptr) env_.save_toast->on_save(i);

        const auto payload = mem.view(0, static_cast<std::size_t>(bytes));
        auto w = env_.files->write_all_sync(path_for(i), payload);
        if (!w) {

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
    if (env_.clock == nullptr || env_.files == nullptr) {
        return std::unexpected(Error{Errc::negotiation, ERR_SITE(), 0});
    }

    const auto now = env_.clock->now();
    if (next_scan_ && now < *next_scan_) return {};
    next_scan_ = now + kScanPeriod;

    return scan_slots();
}

Ex<void> SaveStateEngine::detach() {
    Ex<void> result{};

    if (mapped_ && armed_ && env_.files != nullptr) result = scan_slots();

    drop_slots();
    armed_ = false;
    window_ = SaveStateDecl{};
    next_scan_.reset();
    return result;
}

}  // namespace mister::proto
