// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "app/path_text.h"
#include "infra/error.h"
#include "infra/seat.h"

namespace mister::svc {
class Vfs;
}

namespace mister::app {

class PlayedHistory {
    TASTY_SEAT_RESIDENT(Ui);

public:
    static constexpr std::size_t kMax = 32;

    struct Record {
        std::string core_image;
        std::string core_name;
        std::string path;
        std::string title;
        std::uint8_t ioctl_index = 0;
        char slot = 0;
        std::int64_t unix_time = 0;
    };

    static constexpr std::size_t kCoreImageCap = app::kPathMax + 1;
    static constexpr std::size_t kCoreNameCap = 64;
    static constexpr std::size_t kPathCap = app::kPathMax + 1;
    static constexpr std::size_t kTitleCap = 65;

    static constexpr std::uint32_t kMagic = 0x594C5047u;
    static constexpr std::uint16_t kVersion = 2;

    explicit PlayedHistory(const svc::Vfs& vfs) noexcept : vfs_(&vfs) {}

    [[nodiscard]] Ex<void> load();

    [[nodiscard]] Ex<void> record(const Record& r);

    [[nodiscard]] bool loaded() const noexcept { return loaded_; }

    [[nodiscard]] std::span<const Record> records() const noexcept { return records_; }

    [[nodiscard]] std::uint32_t loads() const noexcept { return loads_; }

    [[nodiscard]] std::uint32_t saves() const noexcept { return saves_; }

    [[nodiscard]] std::uint32_t errors() const noexcept { return errors_; }

    [[nodiscard]] std::uint32_t rejected() const noexcept { return rejected_; }

    [[nodiscard]] std::uint32_t refusals() const noexcept { return refusals_; }

    [[nodiscard]] std::uint32_t clamped() const noexcept { return clamped_; }

    static constexpr std::size_t kHeaderBytes = 8;
    static constexpr std::size_t kRecordBytes = 2192;
    static constexpr std::size_t kFileBytes = kHeaderBytes + kMax * kRecordBytes;

private:
    struct Header {
        std::uint32_t magic;
        std::uint16_t version;
        std::uint16_t count;
    };

    struct Stored {
        std::int64_t unix_time;
        char core_image[kCoreImageCap];
        char core_name[kCoreNameCap];
        char path[kPathCap];
        char title[kTitleCap];
        std::uint8_t ioctl_index;
        char slot;
    };

    static_assert(sizeof(Header) == kHeaderBytes, "played.dat header size");
    static_assert(std::is_trivially_copyable_v<Stored>, "played.dat writes raw records");
    static_assert(
        kPathCap == app::kPathMax + 1 && kCoreImageCap == app::kPathMax + 1,
        "played.dat path and image caps follow PathText"
    );

    static_assert(sizeof(Stored) == kRecordBytes, "played.dat record size is the format");

    [[nodiscard]] static bool names_a_game(const Record& r) noexcept {
        return r.slot != 0 && !r.path.empty() && !r.core_image.empty();
    }

    [[nodiscard]] static bool fields_fit(const Record& r) noexcept {
        return r.core_image.size() < kCoreImageCap
            && r.core_name.size() < kCoreNameCap
            && r.path.size() < kPathCap
            && r.title.size() < kTitleCap;
    }

    [[nodiscard]] Ex<void> save_();

    const svc::Vfs* vfs_;
    std::vector<Record> records_;
    bool loaded_ = false;
    std::uint32_t loads_ = 0;
    std::uint32_t saves_ = 0;
    std::uint32_t errors_ = 0;
    std::uint32_t rejected_ = 0;
    std::uint32_t refusals_ = 0;
    std::uint32_t clamped_ = 0;
};

}
