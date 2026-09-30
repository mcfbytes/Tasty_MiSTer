// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "infra/error.h"
#include "os/clock.h"
#include "hal/fpga_memory.h"
#include "hal/phys_region.h"
#include "proto/conf_str.h"

namespace mister::proto {

class SaveStateEngine {
public:
    static constexpr unsigned kSlots = 4;

    static constexpr std::uint32_t kMaxSlotBytes = 128u * 1024u * 1024u;

    explicit SaveStateEngine(hal::PhysRegion aperture) noexcept : aperture_(aperture) {}

    static constexpr std::size_t kHeaderBytes = 8;
    static constexpr std::uint32_t kCounterWord = 0;
    static constexpr std::uint32_t kSizeWord = 1;

    static constexpr std::uint32_t kSentinel = 0xFFFF'FFFFu;

    static constexpr std::chrono::nanoseconds kScanPeriod{std::chrono::seconds{1}};

    static constexpr std::string_view kDir = "savestates";
    static constexpr std::string_view kArcadeSubdir = "Arcade";
    static constexpr std::string_view kExt = ".ss";

    class ISaveStateFiles {
    public:
        virtual ~ISaveStateFiles() = default;

        virtual Ex<std::size_t> read_all(std::string_view path, std::span<std::byte> dst) = 0;

        virtual Ex<void> write_all_sync(std::string_view path, std::span<const std::byte> src) = 0;

    protected:
        ISaveStateFiles() = default;
        ISaveStateFiles(const ISaveStateFiles&) = default;
        ISaveStateFiles& operator=(const ISaveStateFiles&) = default;
    };

    class ISaveToast {
    public:
        virtual ~ISaveToast() = default;

        virtual void on_save(unsigned slot) = 0;

    protected:
        ISaveToast() = default;
        ISaveToast(const ISaveToast&) = default;
        ISaveToast& operator=(const ISaveToast&) = default;
    };

    struct Environment {
        ISaveStateFiles* files = nullptr;
        const os::IClock* clock = nullptr;

        ISaveToast* save_toast = nullptr;

        std::span<std::byte> host_backing{};
    };

    struct SaveTarget {
        std::string_view core_name;
        std::string_view rom_path;
        bool arcade = false;
    };

    void bind(const Environment& env);

    Ex<void> arm(const SaveTarget& target);
    void disarm() noexcept;
    bool armed() const noexcept { return armed_; }

    Ex<void> attach(const SaveStateDecl& ss);

    Ex<void> rebind(const SaveStateDecl& ss);

    Ex<void> poll();

    Ex<void> detach();

    bool window_valid(const SaveStateDecl& ss) const noexcept;

    static std::string savestate_dir(const SaveTarget& target);
    static std::string savestate_stem(std::string_view rom_path);
    static std::string savestate_path(const SaveTarget& target, unsigned slot);
    static std::string savestate_path_stock(const SaveTarget& target);

    bool mapped() const noexcept { return mapped_; }
    const SaveStateDecl& window() const noexcept { return window_; }
    std::uint32_t shadow_counter(unsigned slot) const;
    unsigned saves_written() const noexcept { return saves_written_; }
    unsigned load_failures() const noexcept { return load_failures_; }

    std::uint32_t span_warnings() const noexcept { return span_warnings_; }

    hal::PhysRegion aperture() const noexcept { return aperture_; }

private:
    Ex<void> map_slots(const SaveStateDecl& ss, bool remap);
    Ex<void> prime_slots();
    Ex<void> scan_slots();
    void drop_slots() noexcept;
    std::string path_for(unsigned slot) const;
    hal::PhysRegion region_for(const SaveStateDecl& ss, unsigned slot) const;

    hal::PhysRegion aperture_;
    Environment env_{};
    SaveStateDecl window_{};

    std::optional<hal::FpgaMemory> regions_[kSlots];
    std::uint32_t ss_cnt_[kSlots]{};

    std::string subdir_;
    std::string stem_;
    bool armed_ = false;
    bool mapped_ = false;

    bool ever_attached_ = false;

    std::optional<std::chrono::nanoseconds> next_scan_;
    unsigned saves_written_ = 0;
    unsigned load_failures_ = 0;
    std::uint32_t span_warnings_ = 0;
};

}  // namespace mister::proto
