// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/pcecd_ladder.h"

#include <cstring>
#include <memory>
#include <vector>

#include "infra/byte_view.h"
#include "svc/file.h"

namespace mister::cores {
namespace {

constexpr std::uint64_t kPceBiosCap = 262144;

constexpr std::size_t kPceChunk = 1024;

std::uint8_t bit_reverse(std::uint8_t c) {
    return static_cast<std::uint8_t>(
        ((c & 1) << 7)
        | ((c & 2) << 5)
        | ((c & 4) << 3)
        | ((c & 8) << 1)
        | ((c & 16) >> 1)
        | ((c & 32) >> 3)
        | ((c & 64) >> 5)
        | ((c & 128) >> 7)
    );
}

}

Ex<AssetWait> PceCdLadder::load_pce_bios(const BootAsset& row) {
    if (pieces_)
        return finish_pce_bios_();
    if (load_path_.empty()) {
        auto p = resolve_row(row);
        if (!p)
            return AssetWait::Miss;
        (void)load_path_.assign(*p);
    }
    auto f = vfs_.open(load_path_.view(), svc::OpenMode::ReadWhole);
    if (!f) {
        load_path_.clear();
        return AssetWait::Miss;
    }
    auto szr = (*f)->size();
    if (!szr) {
        load_path_.clear();
        return AssetWait::Miss;
    }
    const std::uint64_t file_size = szr->v;

    const std::uint64_t start = file_size & 0x3FFu;
    std::uint64_t size = file_size;
    std::uint8_t swap = 0;
    if (size >= kPceBiosCap) {
        size = kPceBiosCap;

        for (const auto& srow : profile_.signatures) {
            if (srow.region == 0 || srow.magic.empty())
                continue;
            std::byte probe[64];
            const std::size_t n = srow.magic.size() > sizeof(probe) ? sizeof(probe)
                                                                    : srow.magic.size();
            auto rd = (*f)->read_at(start + srow.offset, std::span<std::byte>(probe, n));
            if (!rd)
                break;
            if (*rd == n && std::memcmp(probe, srow.magic.data(), n) == 0) {
                swap = srow.region;
            }
            break;
        }
    }

    std::uint8_t us_cart = 0;
    std::vector<std::uint8_t> payload;
    payload.reserve(static_cast<std::size_t>(size));
    std::uint8_t buf[kPceChunk];
    std::uint64_t off = start;
    while (size != 0) {
        const auto chunk = static_cast<std::size_t>(size > kPceChunk ? kPceChunk : size);
        size -= chunk;
        auto rd = (*f)->read_at(off, std::as_writable_bytes(std::span(buf)).first(chunk));
        if (!rd)
            return std::unexpected(rd.error());
        const std::size_t got = *rd;
        off += got;
        if (swap != 0) {
            for (std::size_t i = 0; i < got; ++i)
                buf[i] = bit_reverse(buf[i]);
        } else if (file_size >= kPceBiosCap) {

            if (got >= 8) {
                for (std::size_t i = 0; i + 8 <= got - 1; ++i) {
                    if (std::memcmp(buf + i, "ALL DATA", 8) == 0)
                        us_cart = 1;
                }
            }
        }
        payload.insert(payload.end(), buf, buf + got);
        if (got < chunk)
            break;
    }

    const WideIoIndex dest = (marker_seen_ && row.dest_if_marker.v != 0) ? row.dest_if_marker
                                                                         : row.dest;

    const auto region = static_cast<std::uint8_t>(swap | us_cart);
    auto p = PayloadPieces::of_bytes(
        std::move(payload),
        {},
        {.dest = dest, .region = region, .chunk = static_cast<std::uint16_t>(kPceChunk)}
    );
    if (!p)
        return std::unexpected(p.error());
    pieces_.emplace(std::move(*p));
    pce_region_ = region;
    return finish_pce_bios_();
}

Ex<AssetWait> PceCdLadder::finish_pce_bios_() {
    auto w = step_pieces_();
    if (!w || *w != AssetWait::Ready)
        return w;
    report_.region = pce_region_;
    report_.region_written = true;
    return AssetWait::Ready;
}

Ex<bool> PceCdLadder::on_step() {
    TASTY_SEAT_BODY(PceCdLadder);
    switch (rung_) {
        case Rung::MountDisc: {

            if (image_.empty()) {
                if (mount_bounded(std::string_view{}) == MountState::Pending)
                    return false;
                rung_ = Rung::Eject;
                return false;
            }
            const MountState ms = mount_bounded(image_);
            if (ms == MountState::Pending)
                return false;
            if (ms == MountState::Failed) {
                rung_ = Rung::Eject;
                return false;
            }
            report_.disc_mounted = true;
            rung_ = Rung::SgxProbe;
            return false;
        }

        case Rung::SgxProbe: {

            for (const auto& row : profile_.boot_assets) {
                if (row.marker_sibling.empty())
                    continue;
                BootAsset probe = row;
                probe.name = row.marker_sibling;
                auto p = resolve_row(probe);
                if (p && vfs_.open(*p, svc::OpenMode::Read))
                    marker_seen_ = true;
                break;
            }
            rung_ = Rung::Bios;
            row_i_ = 0;
            return false;
        }

        case Rung::Bios: {

            if (!save_ready_) {
                if (save_path_.empty()) {
                    auto sp = save_path_for(image_);
                    if (!sp)
                        return std::unexpected(sp.error());
                    save_path_ = *sp;
                }
                if (!prepare_save(save_path_))
                    return false;
                save_ready_ = true;
            }

            const auto rows = profile_.boot_assets;
            while (row_i_ < rows.size() && rows[row_i_].group == 0) {
                const BootAsset& row = rows[row_i_];
                auto hit = row.loader == AssetLoader::PceBios ? load_pce_bios(row) : load_row(row);
                if (!hit)
                    return std::unexpected(hit.error());
                if (*hit == AssetWait::Pending)
                    return false;
                ++row_i_;
                if (*hit == AssetWait::Ready) {

                    if (!order_mount_save(save_path_, false))
                        return false;
                    report_.save_mounted = true;
                    report_.bios_found = true;
                    rung_ = Rung::NotifyMount;
                    return false;
                }
                return false;
            }

            ++report_.bios_missing;
            if (group_gates(0)) {
                rung_ = Rung::Done;
                return true;
            }
            rung_ = Rung::NotifyMount;
            return false;
        }

        case Rung::NotifyMount:

            if (!order_notify_mount(true))
                return false;
            rung_ = Rung::Done;
            return true;

        case Rung::Eject:

            if (sub_ == 0) {
                if (!order_notify_mount(false))
                    return false;
                sub_ = 1;
            }
            if (!order_status_bit0(true))
                return false;
            bit0_asserted_ = true;
            pulse_release_ns_ = now_ns()
                + static_cast<std::int64_t>(profile_.staging.reset_pulse_us) * 1000;
            sub_ = 0;
            rung_ = Rung::PulseRelease;
            return false;

        case Rung::PulseRelease:
            if (bit0_asserted_) {
                if (now_ns() < pulse_release_ns_)
                    return false;
                if (!order_status_bit0(false))
                    return false;
                bit0_asserted_ = false;
            }
            rung_ = Rung::Done;
            return true;

        case Rung::Done:
            return true;
    }
    return true;
}

}
