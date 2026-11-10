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

    [[nodiscard]] Ex<std::span<std::byte>> read(proto::Lba lba, std::span<std::byte> dst);

    void reset() noexcept {
        sub_.reset();
        sub_origin_ = proto::Lba{};
        sub_cdg_ = false;
    }

private:
    [[nodiscard]] proto::BlockCount record_at(proto::Lba lba) const noexcept {
        return lba > sub_origin_ ? lba - sub_origin_ : proto::BlockCount{};
    }

    [[nodiscard]] Ex<std::span<std::byte>> read_cdg_subcode_(
        proto::BlockCount at,
        std::span<std::byte> dst
    );

    std::unique_ptr<IFile> sub_;

    proto::Lba sub_origin_{};
    bool sub_cdg_ = false;
};

}
