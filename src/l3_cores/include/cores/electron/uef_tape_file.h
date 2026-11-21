// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "infra/prelude.h"
#include "infra/seat.h"
#include "proto/types.h"
#include "svc/file.h"

namespace mister::cores::electron::uef::detail {

struct TapeState;

class UefTapeFile final : public svc::IFile {
    TASTY_SEAT_RESIDENT(Main);

public:
    explicit UefTapeFile(std::vector<std::uint8_t>&& image);
    ~UefTapeFile() override;

    [[nodiscard]] Ex<infra::ByteCount> read_at(
        proto::FileOffset at,
        std::span<std::byte> dst
    ) override;

    [[nodiscard]] Ex<infra::ByteCount> write_at(
        proto::FileOffset,
        std::span<const std::byte>
    ) override;

    [[nodiscard]] Ex<proto::FileSize> size() const override;
    [[nodiscard]] Ex<void> flush() override;

private:
    std::unique_ptr<TapeState> state_;
};

}
