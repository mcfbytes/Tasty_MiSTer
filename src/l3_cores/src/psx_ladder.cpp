// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/psx_ladder.h"

#include <cstddef>

#include "cores/cd_msf.h"
#include "cores/file_slot.h"
#include "infra/ascii.h"
#include "infra/byte_view.h"
#include "proto/types.h"
#include "svc/file.h"
#include "svc/msf.h"

namespace mister::cores {
namespace {

constexpr std::size_t kSbiHeader = 4;
constexpr std::size_t kSbiBlock = 14;

constexpr IoIndex kCapturedMenuRow{1};

constexpr HeadLba kLibCryptSectors[16] = {
    HeadLba{14105},
    HeadLba{14231},
    HeadLba{14485},
    HeadLba{14579},
    HeadLba{14649},
    HeadLba{14899},
    HeadLba{15056},
    HeadLba{15130},
    HeadLba{15242},
    HeadLba{15312},
    HeadLba{15378},
    HeadLba{15628},
    HeadLba{15919},
    HeadLba{16031},
    HeadLba{16101},
    HeadLba{16167},
};

std::uint8_t bcd_to_dec(std::uint8_t bcd) {
    return proto::PackedBcd{bcd}.value();
}

}

void PsxLadder::decide_stage_edge_() {
    stage_save_ = false;
    stage_assets_ = false;
    facts_.reset = false;
    if (!mount_answer().plan.data_first_track)
        return;

    const std::string_view cur_dir = dir_or_empty(image_path());
    if (!last_dir_.empty() && cur_dir == last_dir_)
        return;

    if (!noreset_ && !last_dir_.empty()) {
        std::string probe = last_dir_;
        probe += "/noreset.txt";
        noreset_ = vfs_.open(probe, svc::OpenMode::Read).has_value();
    }
    facts_.reset = !noreset_;
    last_dir_.assign(cur_dir);
    stage_assets_ = facts_.reset;
    stage_save_ = true;
}

std::uint16_t PsxLadder::scan_libcrypt_() const {
    std::unique_ptr<svc::IFile> sbi;
    const std::string_view game_id = mount_answer().plan.game_id.view();
    if (!game_id.empty()) {
        std::string zipped = "games/";
        zipped.append(profile_.name);
        zipped += "/sbi.zip/";
        zipped.append(game_id);
        zipped += ".sbi";
        if (auto f = vfs_.open(zipped, svc::OpenMode::Read))
            sbi = std::move(*f);
    }
    if (sbi == nullptr && !image_path().empty()) {
        const std::string_view img = image_path();
        std::string cand{img.size() > 4 ? img.substr(0, img.size() - 4) : img};
        cand += ".sbi";
        if (auto f = vfs_.open(cand, svc::OpenMode::Read))
            sbi = std::move(*f);
    }
    if (sbi == nullptr)
        return 0;

    std::uint8_t buf[1024];
    auto rd = sbi->read_at(0, infra::byte_cast<std::byte>(std::span(buf)));
    if (!rd || *rd == 0)
        return 0;
    const std::size_t sz = *rd;
    std::uint16_t mask = 0;
    for (std::size_t i = 0;; ++i) {
        const std::size_t pos = kSbiHeader + i * kSbiBlock;
        if (pos + 3 > sz)
            break;
        const svc::Msf msf{
            bcd_to_dec(buf[pos]),
            bcd_to_dec(buf[pos + 1]),
            bcd_to_dec(buf[pos + 2])
        };
        const HeadLba lba = HeadLba::of(msf);
        for (unsigned m = 0; m < 16; ++m) {
            if (kLibCryptSectors[m] == lba)
                mask |= static_cast<std::uint16_t>(1u << (15 - m));
        }
    }
    return mask;
}

WideIoIndex PsxLadder::menu_file_index() const {
    std::uint8_t ext_idx = 0;
    const std::string_view ext = ext_of(image_path());
    const std::string_view want = ext.empty() ? ext : ext.substr(1);
    for (const FileSlot& s : profile_.slots) {
        if (s.index != profile_.staging.disc_slot)
            continue;
        const std::string_view fields = s.extensions;
        for (std::size_t o = 0; o + 3 <= fields.size(); o += 3) {
            const std::string_view cand = fields.substr(o, 3);
            if (infra::ieq(cand, want)) {
                ext_idx = static_cast<std::uint8_t>(o / 3);
                break;
            }
        }
        break;
    }
    return WideIoIndex::of(kCapturedMenuRow, ext_idx);
}

Ex<bool> PsxLadder::on_step() {
    TASTY_SEAT_BODY(PsxLadder);
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
            decide_stage_edge_();
            facts_.libcrypt_mask = scan_libcrypt_();
            report_.same_game = !stage_save_;
            report_.bios_found = false;
            rung_ = Rung::Bios;
            row_i_ = 0;
            return false;
        }

        case Rung::Bios: {

            if (!stage_assets_) {
                rung_ = Rung::MemCard;
                return false;
            }
            const auto rows = profile_.boot_assets;
            if (row_i_ < rows.size()) {
                const BootAsset& row = rows[row_i_];
                auto hit = load_row(row);
                if (!hit)
                    return std::unexpected(hit.error());
                if (*hit == AssetWait::Pending)
                    return false;
                ++row_i_;
                if (*hit == AssetWait::Ready) {
                    report_.bios_found = true;
                    rung_ = Rung::MemCard;
                }
                return false;
            }
            rung_ = Rung::MemCard;
            return false;
        }

        case Rung::MemCard: {

            const bool automount_off = host_.status_word().get_bit(proto::StatusBit{63});
            if (stage_save_ && !automount_off) {

                if (auto r = resolve_dir_save_(image_dir()); !r)
                    return std::unexpected(r.error());
                if (!prepare_save(save_name()))
                    return false;

                if (sub_ == 0) {
                    if (!profile_.staging.save
                        || !order_set_index(WideIoIndex::of(profile_.staging.save->slot)))
                        return false;
                    sub_ = 1;
                }
                if (!order_mount_save(save_name(), false))
                    return false;
                report_.save_mounted = true;
                sub_ = 0;
            }
            rung_ = Rung::DiscPayload;
            return false;
        }

        case Rung::DiscPayload:

            if (sub_ == 0) {
                ss_gen_ = next_gen();
                ss_due_ns_ = now_ns() + kSaveStateHoldBound;
                sub_ = host_.order_savestates(image_path(), ss_gen_) ? 1 : 2;
            }
            if (sub_ == 1) {
                if (!host_.savestates_answered().at_or_past(ss_gen_)) {
                    if (now_ns() < ss_due_ns_)
                        return false;
                    report_.savestate_hold_expired = true;
                }
                sub_ = 2;
            }

            if (!order_disc_payload(facts_))
                return false;
            sub_ = 0;
            rung_ = Rung::Announce;
            return false;

        case Rung::Announce: {

            if (sub_ == 0) {
                if (!order_set_index(menu_file_index()))
                    return false;
                sub_ = 1;
            }
            if (!order_announce_disc(mount_answer().plan.disc_size_bytes))
                return false;
            sub_ = 0;
            rung_ = Rung::Done;
            return true;
        }

        case Rung::Eject:

            if (!order_announce_disc(proto::FileSize{}))
                return false;
            rung_ = Rung::Done;
            return true;

        case Rung::Done:
            return true;
    }
    return true;
}

}
