// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/arcade_core.h"

#include <memory>
#include <span>
#include <utility>

#include "proto/image_bracket.h"
#include "proto/image_sink.h"

namespace mister::cores {

ArcadeCore::ArcadeCore(const CoreProfile& profile, const HostServices& host, ArcadeFacts facts)
    : Core(profile, host),
      dip_name_(std::move(facts.dip_name)),
      nvm_name_(std::move(facts.nvm_name)),
      switches_(std::move(facts.switches)),
      dip_cur_(facts.dip_cur),
      dip_saved_(facts.dip_cur),
      nvram_(std::move(facts.nvram)) {}

Ex<void> ArcadeCore::send_dips_() {
    if (switches_.rows.empty())
        return {};
    auto session = proto::ImageBracket::open(image_sink(), kDipIndex);
    if (!session)
        return std::unexpected(session.error());
    const mra::DipWord::Bytes pack = dip_bytes();
    if (auto r = session->write(std::span<const std::uint8_t>(pack)); !r)
        return r;
    return session->end();
}

SaveImage ArcadeCore::save_image() const noexcept {
    TASTY_SEAT_BODY(ArcadeCore);
    if (!nvram_)
        return {};
    return SaveImage{.index = nvram_->index, .size = nvram_->size.detail()};
}

namespace {

proto::DipChoiceCount choices_of(const mra::DipRow& d) noexcept {
    return proto::DipChoiceCount::of(d.choices.size());
}

}

proto::DipChoice ArcadeCore::current_choice_(const mra::DipRow& d) const noexcept {
    const mra::DipWord status = dip_cur_.masked(d.field.mask());
    const proto::DipChoiceCount choices = choices_of(d);
    for (proto::DipChoice n{}; n.within(choices); n = n.after()) {
        if (status == n.pick(d.choices).val)
            return n;
    }
    return {};
}

std::size_t ArcadeCore::dip_count() const noexcept {
    TASTY_SEAT_BODY(ArcadeCore);
    return switches_.rows.size();
}

DipRowView ArcadeCore::dip_row(proto::DipRowIndex row) const noexcept {
    TASTY_SEAT_BODY(ArcadeCore);
    if (!row.indexes(switches_.rows))
        return {};
    const mra::DipRow& d = row.pick(switches_.rows);
    DipRowView v;
    v.name = d.name;
    v.choices = choices_of(d);
    v.current = current_choice_(d);
    return v;
}

std::string_view ArcadeCore::dip_choice(
    proto::DipRowIndex row,
    proto::DipChoice choice
) const noexcept {
    TASTY_SEAT_BODY(ArcadeCore);
    if (!row.indexes(switches_.rows))
        return {};
    const mra::DipRow& d = row.pick(switches_.rows);
    if (!choice.indexes(d.choices))
        return {};
    return choice.pick(d.choices).id;
}

Ex<void> ArcadeCore::set_dip(proto::DipRowIndex row, proto::DipChoice choice) {
    TASTY_SEAT_BODY(ArcadeCore);
    if (!row.indexes(switches_.rows)) {
        return std::unexpected(
            Error{Errc::slot_range, ERR_SITE(), static_cast<std::uint32_t>(row.v)}
        );
    }
    const mra::DipRow& d = row.pick(switches_.rows);
    if (!choice.within(choices_of(d))) {
        return std::unexpected(
            Error{Errc::slot_range, ERR_SITE(), static_cast<std::uint32_t>(choice.v)}
        );
    }
    dip_cur_ = dip_cur_.with(d.field.mask(), choice.pick(d.choices).val);
    return send_dips_();
}

std::array<std::uint8_t, 8> ArcadeCore::dip_bytes() const noexcept {
    TASTY_SEAT_BODY(ArcadeCore);
    return dip_cur_.le_bytes();
}

bool ArcadeCore::dips_dirty() const noexcept {
    TASTY_SEAT_BODY(ArcadeCore);
    return !switches_.rows.empty() && dip_saved_ != dip_cur_;
}

Ex<void> ArcadeCore::on_mount(IoIndex slot, const MountedPath& p) {
    return stream_.mount(slot, p);
}

void ArcadeCore::on_set_pending_file_ext(std::string_view ext) noexcept {
    stream_.set_pending_ext(ext);
}

std::uint64_t ArcadeCore::on_last_tx_bytes() const noexcept {
    return stream_.bytes();
}

svc::Crc32 ArcadeCore::on_last_tx_crc() const noexcept {
    return stream_.crc();
}

std::unique_ptr<Core> make_arcade(const CoreProfile& p, const CoreGrant& g) {
    ArcadeDocument doc = read_arcade_document(g.manifest.text, g.manifest.path, g.services.vfs)
                             .value_or(ArcadeDocument{});
    return std::make_unique<ArcadeCore>(p, g.services, std::move(doc.facts));
}

}
