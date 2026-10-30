// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/arcade_core.h"

#include <algorithm>
#include <memory>
#include <span>
#include <utility>

#include "proto/image_bracket.h"
#include "proto/image_sink.h"

namespace mister::cores {

ArcadeCore::ArcadeCore(const CoreProfile& profile, const HostServices& host, ArcadeFacts facts)
    : Core(profile, host),
      file_host_(kGenericProfile, host),
      btn_names_(std::move(facts.btn_names)),
      btn_defaults_(std::move(facts.btn_defaults)),
      dip_name_(std::move(facts.dip_name)),
      nvm_name_(std::move(facts.nvm_name)),
      switches_(std::move(facts.switches)),
      cheats_(std::move(facts.cheats)),
      cheats_dropped_(facts.cheats_dropped),
      dip_cur_(facts.dip_cur),
      dip_saved_(facts.dip_cur),
      nvram_index_(facts.nvram_index),
      nvram_size_(facts.nvram_size) {}

Ex<void> ArcadeCore::send_dips_() {
    if (switches_.rows.empty())
        return {};
    auto session = proto::ImageBracket::open(image_sink(), proto::WideIoIndex{kDipIndex});
    if (!session)
        return std::unexpected(session.error());
    const std::array<std::uint8_t, 8> pack = dip_bytes();
    if (auto r = session->write(std::span<const std::uint8_t>(pack)); !r)
        return r;
    return session->end();
}

bool ArcadeCore::nvram_declared_() const noexcept {
    return nvram_declared(nvram_index_, nvram_size_);
}

SaveImage ArcadeCore::save_image() const noexcept {
    TASTY_SEAT_BODY(ArcadeCore);
    if (!nvram_declared_())
        return {};
    return SaveImage{
        .index = proto::WideIoIndex{static_cast<std::uint8_t>(nvram_index_)},
        .size = static_cast<std::uint32_t>(nvram_size_)
    };
}

std::uint8_t ArcadeCore::current_choice_(const mra::DipRow& d) const noexcept {
    const std::uint64_t status = dip_cur_ & d.mask;
    for (int n = 0; n < d.num; ++n) {
        if (status == d.val[static_cast<std::size_t>(n)])
            return static_cast<std::uint8_t>(n);
    }
    return 0;
}

std::size_t ArcadeCore::dip_count() const noexcept {
    TASTY_SEAT_BODY(ArcadeCore);
    return switches_.rows.size();
}

DipRowView ArcadeCore::dip_row(std::size_t row) const noexcept {
    TASTY_SEAT_BODY(ArcadeCore);
    if (row >= switches_.rows.size())
        return {};
    const mra::DipRow& d = switches_.rows[row];
    DipRowView v;
    v.name = d.name;
    v.choices = static_cast<std::uint8_t>(d.num < 0 ? 0 : d.num);
    v.current = current_choice_(d);
    return v;
}

std::string_view ArcadeCore::dip_choice(std::size_t row, std::size_t choice) const noexcept {
    TASTY_SEAT_BODY(ArcadeCore);
    if (row >= switches_.rows.size())
        return {};
    const mra::DipRow& d = switches_.rows[row];
    if (choice >= d.ids.size())
        return {};
    return d.ids[choice];
}

Ex<void> ArcadeCore::set_dip(std::size_t row, std::size_t choice) {
    TASTY_SEAT_BODY(ArcadeCore);
    if (row >= switches_.rows.size()) {
        return std::unexpected(
            Error{Errc::slot_range, ERR_SITE(), static_cast<std::uint32_t>(row)}
        );
    }
    const mra::DipRow& d = switches_.rows[row];
    if (d.num <= 0 || choice >= static_cast<std::size_t>(d.num)) {
        return std::unexpected(
            Error{Errc::slot_range, ERR_SITE(), static_cast<std::uint32_t>(choice)}
        );
    }
    dip_cur_ = (dip_cur_ & ~d.mask) | d.val[choice];
    return send_dips_();
}

std::array<std::uint8_t, 8> ArcadeCore::dip_bytes() const noexcept {
    TASTY_SEAT_BODY(ArcadeCore);
    return dip_pack(dip_cur_);
}

bool ArcadeCore::dips_dirty() const noexcept {
    TASTY_SEAT_BODY(ArcadeCore);
    return !switches_.rows.empty() && dip_saved_ != dip_cur_;
}

Ex<void> ArcadeCore::on_mount(IoIndex slot, const MountedPath& p) {
    return file_host_.mount(slot, p);
}

void ArcadeCore::on_set_pending_file_ext(std::string_view ext) noexcept {
    file_host_.set_pending_ext(ext);
}

std::uint64_t ArcadeCore::on_last_tx_bytes() const noexcept {
    return file_host_.bytes_sent();
}

std::uint32_t ArcadeCore::on_last_tx_crc() const noexcept {
    return file_host_.tx_crc();
}

std::string_view ArcadeCore::cheat_name(std::size_t i) const noexcept {
    TASTY_SEAT_BODY(ArcadeCore);
    return cheats_.cheat_name(i);
}

std::span<const std::uint8_t> ArcadeCore::cheat_bytes(std::size_t i) const noexcept {
    TASTY_SEAT_BODY(ArcadeCore);
    return cheats_.cheat_bytes(i);
}

std::unique_ptr<Core> make_arcade(const CoreProfile& p, const CoreGrant& g) {
    ArcadeDocument doc = read_arcade_document(g.manifest.text, g.manifest.path, g.services.vfs);
    return std::make_unique<ArcadeCore>(p, g.services, std::move(doc.facts));
}

}
