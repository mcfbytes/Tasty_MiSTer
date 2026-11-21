// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>
#include <span>

#include "infra/error.h"
#include "infra/prelude.h"
#include "svc/disc/disc_ask.h"

namespace mister::svc::disc {

class IDiscReader {
public:
    virtual ~IDiscReader() = default;
    IDiscReader(const IDiscReader&) = delete;
    IDiscReader& operator=(const IDiscReader&) = delete;
    IDiscReader(IDiscReader&&) = delete;
    IDiscReader& operator=(IDiscReader&&) = delete;

    [[nodiscard]] virtual Ex<std::span<std::byte>> read_form(
        DiscForm form,
        proto::Lba lba,
        std::span<std::byte> dst,
        std::optional<SectorHeader> header = {}
    ) = 0;

protected:
    IDiscReader() = default;
};

}
