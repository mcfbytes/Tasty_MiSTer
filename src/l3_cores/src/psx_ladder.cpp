// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/psx_ladder.h"

#include <strings.h>

#include <cstddef>

#include "cores/file_slot.h"
#include "svc/file.h"

namespace mister::cores {
namespace {

constexpr std::size_t kSbiHeader = 4;
constexpr std::size_t kSbiBlock = 14;

constexpr std::uint32_t kLibCryptSectors[16] = {
    14105, 14231, 14485, 14579, 14649, 14899, 15056, 15130,
    15242, 15312, 15378, 15628, 15919, 16031, 16101, 16167,
};

std::uint8_t bcd_to_dec(std::uint8_t bcd) {
    return static_cast<std::uint8_t>((bcd >> 4) * 10 + (bcd & 0x0F));
}

std::uint32_t msf_to_lba(std::uint32_t m, std::uint32_t s, std::uint32_t f) {
    return (m * 60 + s) * 75 + f;
}

std::string_view psx_dir_of(std::string_view path) {
    const auto slash = path.rfind('/');
    return slash == std::string_view::npos ? std::string_view{} : path.substr(0, slash);
}

}  // namespace

void PsxLadder::decide_stage_edge_() {
    stage_save_ = false;
    stage_assets_ = false;
    facts_.reset = false;
    if (!mount_answer().data_first_track) return;
    const std::string_view cur_dir = psx_dir_of(image_);
    if (!last_dir_.empty() && cur_dir == last_dir_) return;

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
    const std::string_view game_id = mount_answer().game_id.view();
    if (!game_id.empty()) {
        std::string zipped = "games/";
        zipped.append(profile_.name);
        zipped += "/sbi.zip/";
        zipped.append(game_id);
        zipped += ".sbi";
        if (auto f = vfs_.open(zipped, svc::OpenMode::Read)) sbi = std::move(*f);
    }
    if (sbi == nullptr && !image_.empty()) {
        std::string cand{image_.size() > 4 ? std::string_view{image_}.substr(0, image_.size() - 4)
                                           : std::string_view{image_}};
        cand += ".sbi";
        if (auto f = vfs_.open(cand, svc::OpenMode::Read)) sbi = std::move(*f);
    }
    if (sbi == nullptr) return 0;

    std::uint8_t buf[1024];
    auto rd = sbi->read_at(0, std::span<std::byte>{reinterpret_cast<std::byte*>(buf), sizeof(buf)});
    if (!rd || *rd == 0) return 0;
    const std::size_t sz = *rd;
    std::uint16_t mask = 0;
    for (std::size_t i = 0;; ++i) {
        const std::size_t pos = kSbiHeader + i * kSbiBlock;
        if (pos + 3 > sz) break;
        const std::uint32_t lba =
            msf_to_lba(bcd_to_dec(buf[pos]), bcd_to_dec(buf[pos + 1]), bcd_to_dec(buf[pos + 2]));
        for (unsigned m = 0; m < 16; ++m) {
            if (kLibCryptSectors[m] == lba) mask |= static_cast<std::uint16_t>(1u << (15 - m));
        }
    }
    return mask;
}

std::uint16_t PsxLadder::menu_file_index() const {
    std::uint16_t ext_idx = 0;
    const std::string_view ext = ext_of(image_);
    const std::string_view want = ext.empty() ? ext : ext.substr(1);
    for (const FileSlot& s : profile_.slots) {
        if (s.index.v != profile_.staging.disc_slot.v) continue;
        const std::string_view fields = s.extensions;
        for (std::size_t o = 0; o + 3 <= fields.size(); o += 3) {
            const std::string_view cand = fields.substr(o, 3);
            if (want.size() == 3 && ::strncasecmp(cand.data(), want.data(), 3) == 0) {
                ext_idx = static_cast<std::uint16_t>(o / 3);
                break;
            }
        }
        break;
    }
    return static_cast<std::uint16_t>((static_cast<std::uint32_t>(ext_idx) << 6) | 1u);
}

Ex<bool> PsxLadder::on_step() {
    switch (rung_) {
        case Rung::MountDisc: {

            if (image_.empty()) {
                if (mount_bounded(std::string_view{}) == MountState::Pending) return false;
                rung_ = Rung::Eject;
                return false;
            }
            const MountState ms = mount_bounded(image_);
            if (ms == MountState::Pending) return false;
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
                if (!hit) return std::unexpected(hit.error());
                if (*hit == AssetWait::Pending) return false;
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
                if (save_path_.empty()) {

                    std::string dir = "saves/";
                    dir.append(profile_.name);
                    if (auto r = vfs_.ensure_dir(dir); !r) return std::unexpected(r.error());
                    save_path_ = dir;
                    save_path_ += '/';
                    save_path_.append(base_of(image_dir_));
                    save_path_ += ".sav";
                }
                if (!prepare_save(save_path_)) return false;

                if (sub_ == 0) {
                    if (!order_set_index(WideIoIndex{profile_.staging.save_slot.v})) return false;
                    sub_ = 1;
                }
                if (!order_mount_save(save_path_, false)) return false;
                report_.save_mounted = true;
                sub_ = 0;
            }
            rung_ = Rung::DiscPayload;
            return false;
        }

        case Rung::DiscPayload:

            if (!order_disc_payload(facts_)) return false;
            rung_ = Rung::Announce;
            return false;

        case Rung::Announce: {

            if (sub_ == 0) {
                if (!order_set_index(WideIoIndex{menu_file_index()})) return false;
                sub_ = 1;
            }
            if (!order_announce_disc(mount_answer().disc_size_bytes)) return false;
            sub_ = 0;
            rung_ = Rung::Done;
            return true;
        }

        case Rung::Eject:

            if (!order_announce_disc(0)) return false;
            rung_ = Rung::Done;
            return true;

        case Rung::Done:
            return true;
    }
    return true;
}

}  // namespace mister::cores
