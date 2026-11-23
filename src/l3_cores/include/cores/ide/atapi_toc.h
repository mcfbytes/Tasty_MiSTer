// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

#include "proto/types.h"
#include "svc/disc/toc.h"
#include "svc/disc/track.h"
#include "svc/types.h"

namespace mister::cores::ide {

class AtapiToc {
public:
    struct Bounds {
        svc::TrackNumber first{};
        svc::TrackNumber last{};
        proto::Lba lead_out{};
    };

    struct Entry {
        proto::Lba start{};
        svc::disc::TrackType type = svc::disc::TrackType::Cdda;
    };

    struct Hit {
        svc::TrackIndex row{};
        bool pregap = false;
    };

    static constexpr std::byte kAdrPosition{0x10};
    static constexpr std::byte kDataControl{0x04};

    [[nodiscard]] static constexpr std::byte adr_control(svc::disc::TrackType type) noexcept {
        return type == svc::disc::TrackType::Cdda ? kAdrPosition : kAdrPosition | kDataControl;
    }

    explicit constexpr AtapiToc(const svc::disc::Toc& toc) noexcept : toc_(toc) {}

    [[nodiscard]] std::optional<Bounds> bounds() const noexcept;

    [[nodiscard]] std::optional<Entry> entry(svc::TrackNumber requested) const noexcept;

    [[nodiscard]] std::optional<Hit> row_at(proto::Lba block) const noexcept;

    [[nodiscard]] svc::disc::Track row(svc::TrackIndex index) const noexcept;

    [[nodiscard]] svc::TrackCount rows() const noexcept;

    [[nodiscard]] proto::Lba lead_out() const noexcept { return toc_.end; }

private:
    const svc::disc::Toc& toc_;
};

}
