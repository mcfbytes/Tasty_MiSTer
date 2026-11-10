// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "infra/error.h"
#include "infra/indexed_array.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "os/clock.h"
#include "hal/fpga_memory.h"
#include "hal/phys_region.h"
#include "proto/save_state_decl.h"
#include "proto/save_state_name.h"
#include "proto/state_slot.h"

namespace mister::proto {

class SaveStateEngine {
    TASTY_SEAT_EXEMPT(component);

public:
    static constexpr unsigned kSlots = kStateSlots.size();

    static constexpr std::size_t kHeaderBytes = 8;
    static constexpr hal::WordOffset kCounterWord{0};
    static constexpr hal::WordOffset kSizeWord{1};

    static constexpr std::uint32_t kSentinel = std::numeric_limits<std::uint32_t>::max();

    static constexpr std::chrono::nanoseconds kScanPeriod{std::chrono::seconds{1}};

    class ISaveStateFiles {
    public:
        virtual ~ISaveStateFiles() = default;

        [[nodiscard]] virtual Ex<std::size_t> read_at(
            std::string_view path,
            std::uint64_t offset,
            std::span<std::byte> dst
        ) = 0;

        [[nodiscard]] virtual Ex<void> write_all_sync(
            std::string_view path,
            std::span<const std::byte> src
        ) = 0;

    protected:
        ISaveStateFiles() = default;
        ISaveStateFiles(const ISaveStateFiles&) = default;
        ISaveStateFiles& operator=(const ISaveStateFiles&) = default;
    };

    class ISaveToast {
    public:
        virtual ~ISaveToast() = default;

        virtual void on_save(StateSlot slot) = 0;

    protected:
        ISaveToast() = default;
        ISaveToast(const ISaveToast&) = default;
        ISaveToast& operator=(const ISaveToast&) = default;
    };

    class ISlotMap {
    public:
        virtual ~ISlotMap() = default;

        [[nodiscard]] virtual Ex<hal::FpgaMemory> map(const hal::PhysRegion& r) = 0;

    protected:
        ISlotMap() = default;
        ISlotMap(const ISlotMap&) = default;
        ISlotMap& operator=(const ISlotMap&) = default;
    };

    struct Environment {
        TASTY_SEAT_EXEMPT(component);
        ISaveStateFiles& files;
        const os::IClock& clock;
        ISlotMap& map;
        infra::OptRef<ISaveToast> toast{};
    };

    SaveStateEngine(hal::PhysRegion aperture, const Environment& env) noexcept
        : aperture_(aperture),
          env_(env) {}

    SaveStateEngine(const SaveStateEngine&) = delete;
    SaveStateEngine& operator=(const SaveStateEngine&) = delete;

    [[nodiscard]] Ex<void> arm(const SaveTarget& target);
    void disarm() noexcept;

    bool armed() const noexcept { return armed_; }

    [[nodiscard]] Ex<void> attach(const SaveStateDecl& ss);

    [[nodiscard]] Ex<void> rebind(const SaveStateDecl& ss);

    [[nodiscard]] Ex<void> poll();

    [[nodiscard]] Ex<void> detach();

    bool window_valid(const SaveStateDecl& ss) const noexcept;

    static hal::PhysRegion region_for(const SaveStateDecl& ss, StateSlot slot);

    bool mapped() const noexcept { return mapped_; }

    const SaveStateDecl& window() const noexcept { return window_; }

    std::uint32_t shadow_counter(StateSlot slot) const;

    unsigned saves_written() const noexcept { return saves_written_; }

    unsigned write_failures() const noexcept { return write_failures_; }

    std::uint32_t span_warnings() const noexcept { return span_warnings_; }

    hal::PhysRegion aperture() const noexcept { return aperture_; }

private:
    [[nodiscard]] Ex<void> map_slots(const SaveStateDecl& ss, bool remap);
    [[nodiscard]] Ex<void> scan_slots();
    void drop_slots() noexcept;
    std::string path_for(StateSlot slot) const;

    hal::PhysRegion aperture_;
    Environment env_;
    SaveStateDecl window_{};

    struct Shadow {
        TASTY_SEAT_EXEMPT(component);
        std::optional<hal::FpgaMemory> region;
        std::uint32_t counter = 0;

        void rearm() noexcept { counter = kSentinel; }

        void drop() noexcept {
            region.reset();
            rearm();
        }
    };

    infra::IndexedArray<Shadow, kSlots, StateSlot> shadows_{};

    SaveStateName files_;
    bool armed_ = false;
    bool mapped_ = false;

    bool ever_attached_ = false;

    std::optional<std::chrono::nanoseconds> next_scan_;
    unsigned saves_written_ = 0;
    unsigned write_failures_ = 0;
    std::uint32_t span_warnings_ = 0;
};

}
