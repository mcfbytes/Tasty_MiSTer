// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

#include "infra/error.h"
#include "infra/seat.h"
#include "proto/types.h"
#include "svc/disc_file_access.h"
#include "svc/vfs.h"

namespace mister::svc {

class SubcodeSidecar {
    TASTY_SEAT_RESIDENT(Io);

public:
    enum class Found : std::uint8_t { None, Sub, Cdg };

    [[nodiscard]] bool attached() const noexcept { return sub_ != nullptr; }

    void attach(std::unique_ptr<IFile> f) noexcept { sub_ = std::move(f); }

    [[nodiscard]] Found open_sub_or_cdg(std::string_view cue_path, const DiscFileAccess& io);

    [[nodiscard]] Ex<std::size_t> read(proto::Lba lba, std::span<std::byte> dst);

    void reset() noexcept {
        sub_.reset();
        sub_origin_ = 0;
        sub_cdg_ = false;
    }

private:
    [[nodiscard]] Ex<std::size_t> read_cdg_subcode_(std::uint32_t at, std::span<std::byte> dst);

    std::unique_ptr<IFile> sub_;

    std::uint32_t sub_origin_ = 0;
    bool sub_cdg_ = false;
};

}
