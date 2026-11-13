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
            if (srow.region.v == 0 || srow.magic.empty())
                continue;
            std::byte probe[64];
            const std::size_t n = srow.magic.size() > sizeof(probe) ? sizeof(probe)
                                                                    : srow.magic.size();
            auto rd = (*f)->read_at(start + srow.offset, std::span<std::byte>(probe, n));
            if (!rd)
                break;
            if (*rd == n && std::memcmp(probe, srow.magic.data(), n) == 0) {
                swap = srow.region.v;
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

    const WideIoIndex dest = bios_dest_(row);

    const proto::RegionCode region{static_cast<std::uint8_t>(swap | us_cart)};
    auto p = PayloadPieces::of_bytes(
        std::move(payload),
        {},
        {.dest = dest, .region = region, .chunk = WireChunk::of(kPceChunk)}
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

            if (image_path().empty()) {
                if (mount_bounded(std::string_view{}) == MountState::Pending)
                    return false;
                rung_ = Rung::Eject;
                return false;
            }
            const MountState ms = mount_bounded(image_path());
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

            BootAsset probe{.name = kSgxSibling, .where = {}, .anchor = AssetAnchor::ImageDir};
            auto p = resolve_row(probe);
            if (p && vfs_.open(*p, svc::OpenMode::Read))
                sgx_seen_ = true;
            rung_ = Rung::Bios;
            row_.rewind();
            return false;
        }

        case Rung::Bios: {

            auto ready = prepare_save_once_();
            if (!ready)
                return std::unexpected(ready.error());
            if (!*ready)
                return false;

            while (const auto at = row_.current_in(BootGroup::First)) {
                const BootAsset& row = *at;
                auto hit = load_pce_bios(row);
                if (!hit)
                    return std::unexpected(hit.error());
                if (*hit == AssetWait::Pending)
                    return false;
                row_.advance();
                if (*hit == AssetWait::Ready) {

                    if (!order_mount_save(save_name(), false))
                        return false;
                    report_.save_mounted = true;
                    report_.bios_found = true;
                    rung_ = Rung::NotifyMount;
                    return false;
                }
                return false;
            }

            ++report_.bios_missing;
            if (group_gates(BootGroup::First)) {
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

            if (half_ == Half::Fresh) {
                if (!order_notify_mount(false))
                    return false;
                half_ = Half::FirstOrdered;
            }
            if (!hold_reset_())
                return false;
            half_ = Half::Fresh;
            rung_ = Rung::PulseRelease;
            return false;

        case Rung::PulseRelease:
            if (!pulse_reset_(std::chrono::microseconds{profile_.staging.reset_pulse_us}))
                return false;
            rung_ = Rung::Done;
            return true;

        case Rung::Done:
            return true;
    }
    return true;
}

}
