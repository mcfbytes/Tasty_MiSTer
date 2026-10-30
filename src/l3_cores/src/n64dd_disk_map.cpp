// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/n64dd_disk_map.h"

#include <algorithm>

namespace mister::cores::n64dd {

Ex<void> DiskMap::build_into(DiskMap& m, const SystemArea& area) noexcept {
    m.count_ = 0;
    m.ram_count_ = 0;
    m.ram_ok_ = false;
    m.ram_bytes_ = 0;
    m.disk_type_ = area.disk_type;
    if (area.disk_type >= kDiskTypes) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), area.disk_type});
    }
    const auto& sys = area.sys;
    std::uint64_t source = 0;
    std::uint32_t lba = 0;
    std::uint32_t starting_block = 0;
    for (std::uint32_t vzone = 0; vzone < kZones; ++vzone) {
        const std::uint32_t pzone = kVzoneToPzone[area.disk_type][vzone];
        const Zone& zone = kZoneTable[pzone];
        const std::uint32_t block_bytes = std::uint32_t{zone.sector_bytes} * kSectorsPerBlock;
        const std::uint32_t defect_start = pzone != 0 ? sys[0x07 + pzone] : 0u;
        const std::uint32_t defect_stop = sys[0x07 + pzone + 1];
        std::array<bool, 158> bad_track{};
        std::uint32_t listed = 0;
        for (std::uint32_t d = defect_start; d < defect_stop; ++d) {

            if (0x20u + d >= sys.size()) {
                m.count_ = 0;
                return std::unexpected(Error{Errc::bad_format, ERR_SITE(), d});
            }
            const std::uint32_t t = sys[0x20 + d];
            if (t < zone.tracks)
                bad_track[t] = true;
            ++listed;
        }
        for (std::uint32_t f = 0; listed < kBadTracksPerZone && f < zone.tracks; ++f) {
            bad_track[zone.tracks - 1u - f] = true;
            ++listed;
        }
        for (std::uint32_t zti = 0; zti < zone.tracks; ++zti) {
            const std::uint32_t zone_track = zone.head != 0 ? zone.tracks - 1u - zti : zti;
            const std::uint32_t track = zone.track_offset + zone_track;
            if (bad_track[zone_track]) {
                m.steps_[m.count_++] = Step{
                    .track = static_cast<std::uint16_t>(track),
                    .head = zone.head,
                    .zone_bytes = static_cast<std::uint8_t>(zone.sector_bytes)
                };
                continue;
            }
            for (std::uint32_t block = 0; block < 2; ++block) {
                if (lba >= kLbaCount) {
                    m.count_ = 0;
                    return std::unexpected(Error{Errc::bad_format, ERR_SITE(), lba});
                }
                const bool dev_system = area.development
                    && (lba == 2 || lba == 3 || lba == 10 || lba == 11);
                m.steps_[m.count_++] = Step{
                    .track = static_cast<std::uint16_t>(track),
                    .lba = static_cast<std::uint16_t>(lba),
                    .head = zone.head,
                    .flat_block = static_cast<std::uint8_t>(starting_block ^ block),
                    .copy_bytes = static_cast<std::uint8_t>(
                        dev_system ? kDevSystemSectorBytes : zone.sector_bytes
                    ),
                    .zone_bytes = static_cast<std::uint8_t>(zone.sector_bytes),
                    .good = true,
                    .bad_lba = area.bad_lba.test(lba)
                };
                source += block_bytes;
                ++lba;
            }
            starting_block ^= 1u;
        }
    }
    if (lba != kLbaCount || source != kNddBytes) {
        m.count_ = 0;
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), lba});
    }

    std::uint64_t ram = 0;
    for (std::size_t i = 0; i < m.count_; ++i) {
        const Step& s = m.steps_[i];
        if (!s.good || s.lba < kRamStartLba[area.disk_type])
            continue;
        m.ram_step_[m.ram_count_] = static_cast<std::uint16_t>(i);
        m.ram_base_[m.ram_count_] = static_cast<std::uint32_t>(ram);
        ++m.ram_count_;
        ram += std::uint64_t{s.zone_bytes} * kSectorsPerBlock;
    }
    m.ram_bytes_ = kRamBytes[area.disk_type];
    m.ram_ok_ = m.ram_bytes_ != 0 && ram == m.ram_bytes_;
    return {};
}

std::size_t DiskMap::ram_index_at_(std::uint64_t ram_off) const noexcept {

    const auto* first = ram_base_.data();
    const auto* last = first + ram_count_;
    const auto* it = std::upper_bound(first, last, ram_off, [](std::uint64_t v, std::uint32_t b) {
        return v < b;
    });
    return it == first ? 0 : static_cast<std::size_t>(it - first) - 1;
}

}
