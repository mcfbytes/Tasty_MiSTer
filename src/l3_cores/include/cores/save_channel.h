// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <optional>
#include <string_view>

#include "infra/error.h"
#include "proto/image_source.h"
#include "proto/types.h"
#include "svc/storage_backend.h"

namespace mister::cores {

class ISaveChannel {
public:
    struct Mount {
        proto::IoIndex slot{};
        proto::FileSize size{};
        std::string_view path;
    };

    virtual ~ISaveChannel() = default;

    [[nodiscard]] virtual std::optional<std::size_t> claim_mount_plan() noexcept = 0;

    [[nodiscard]] virtual Ex<Mount> mount_at(std::size_t i) = 0;

    [[nodiscard]] virtual svc::IStorageBackend* backend(std::size_t i) noexcept = 0;

    [[nodiscard]] virtual proto::IImageSource& descriptor() noexcept = 0;

protected:
    ISaveChannel() = default;
    ISaveChannel(const ISaveChannel&) = default;
    ISaveChannel& operator=(const ISaveChannel&) = default;
};

}  // namespace mister::cores
