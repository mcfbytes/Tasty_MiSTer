// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "cores/sidecar_builder.h"
#include "cores/zx81/char_set.h"
#include "cores/zx81/colour_map.h"
#include "cores/zx81/sidecar_class.h"
#include "infra/seat.h"
#include "proto/types.h"
#include "svc/vfs.h"

namespace mister::cores::zx81 {

class Zx81Sidecars final : public ISidecarBuilder {
    TASTY_SEAT_RESIDENT(Main);

public:
    Zx81Sidecars() noexcept = default;
    Zx81Sidecars(const Zx81Sidecars&) = delete;
    Zx81Sidecars& operator=(const Zx81Sidecars&) = delete;

    static constexpr proto::FileSize kMaxDocBytes{1024u * 1024u};

    [[nodiscard]] SidecarPlan plan(const SidecarAsk& ask, const svc::Vfs& vfs) override;

    [[nodiscard]] std::optional<ColourMap> colour_map(std::string_view xml) const;

    [[nodiscard]] std::optional<CharSet> char_set(std::string_view xml) const;

private:
    [[nodiscard]] static std::optional<std::vector<std::uint8_t>> read_sibling_(
        const svc::Vfs& vfs,
        std::string_view pick,
        SidecarClass which
    );
};

}
