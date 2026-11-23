// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "infra/index_set.h"
#include "infra/seat.h"
#include "proto/disk_address.h"
#include "proto/disk_protocol.h"
#include "proto/link_sel.h"
#include "proto/types.h"

namespace mister::proto {

inline constexpr std::size_t kMaxDiskUnits = 6;
using DiskUnits = infra::IndexSet<DiskUnit, kMaxDiskUnits, std::uint8_t>;

using DiskProtocolSet = infra::IndexSet<DiskProtocol, DiskProtocol::kCount, std::uint8_t>;

struct DiskProtocolRow {
    TASTY_SEAT_EXEMPT(const_shared);
    DiskProtocol protocol{};

    DiskUnits units{};

    LinkSel select = LinkSel::Io;

    ReplyCount status_reads{};
};

inline constexpr auto kDiskProtocols = std::to_array<DiskProtocolRow>({
    {.protocol = DiskProtocol::Acsi, .units = DiskUnits{}.with(DiskUnit{0}).with(DiskUnit{1})},
    {.protocol = DiskProtocol::IsaBus,
     .units = DiskUnits{}
                  .with(DiskUnit{0})
                  .with(DiskUnit{1})
                  .with(DiskUnit{2})
                  .with(DiskUnit{3})
                  .with(DiskUnit{4})
                  .with(DiskUnit{5})},
    {.protocol = DiskProtocol::AmigaFloppy,
     .units = DiskUnits{}.with(DiskUnit{0}).with(DiskUnit{1}).with(DiskUnit{2}).with(DiskUnit{3}),
     .select = LinkSel::Fpga,
     .status_reads = ReplyCount{2}},
});

consteval bool disk_protocol_rows_name_each_label_once() {
    if (kDiskProtocols.size() + 1 != DiskProtocol::kCount)
        return false;
    for (std::size_t i = 0; i < kDiskProtocols.size(); ++i) {
        if (kDiskProtocols[i].protocol == DiskProtocol::None)
            return false;

        if (!DiskProtocolSet::addresses(kDiskProtocols[i].protocol))
            return false;
        if (kDiskProtocols[i].units.empty())
            return false;

        if (kDiskProtocols[i].status_reads.link_words() > DiskAddress::kWords)
            return false;
        for (std::size_t j = i + 1; j < kDiskProtocols.size(); ++j)
            if (kDiskProtocols[j].protocol == kDiskProtocols[i].protocol)
                return false;
    }
    return true;
}

static_assert(
    disk_protocol_rows_name_each_label_once(),
    "kDiskProtocols holds one row per DiskProtocol label but None, with its units, and "
    "no more status reads than an address costs"
);

inline constexpr DiskProtocolRow kUnservedDiskProtocol{};

[[nodiscard]] constexpr const DiskProtocolRow& disk_protocol_row(DiskProtocol protocol) noexcept {
    for (const DiskProtocolRow& row : kDiskProtocols)
        if (row.protocol == protocol)
            return row;
    return kUnservedDiskProtocol;
}

}
