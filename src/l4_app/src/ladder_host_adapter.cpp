// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/ladder_host_adapter.h"

#include <algorithm>
#include <span>
#include <utility>

#include "app/negotiated_core.h"
#include "app/owner_push.h"
#include "app/start_chain.h"
#include "cores/registry.h"
#include "cores/share_root.h"
#include "infra/message_sum.h"
#include "infra/path_parts.h"
#include "infra/prelude.h"

namespace mister::app {

proto::ActKey LadderHostAdapter::next_act() noexcept {
    TASTY_SEAT_BODY(LadderHostAdapter);
    return edges_.act_keys.next();
}

bool LadderHostAdapter::order(const proto::LinkOp& op) {
    TASTY_SEAT_BODY(LadderHostAdapter);
    return push_dropping(edges_.inbox, edges_.tally, op);
}

Ex<proto::FileId> LadderHostAdapter::intern_bytes(
    std::span<const std::uint8_t> bytes,
    std::string_view ext,
    hal::FabricAddr load_addr
) {
    TASTY_SEAT_BODY(LadderHostAdapter);
    std::vector<std::uint8_t> buf(bytes.begin(), bytes.end());
    auto id = edges_.inbox.intern_file(
        FileIntern{.bytes = std::move(buf), .ext = ext, .load_addr = load_addr}
    );
    if (!id)
        return std::unexpected(id.error());
    return *id;
}

Ex<proto::FileId> LadderHostAdapter::intern_piece(
    std::span<const std::uint8_t> bytes,
    std::string_view ext,
    proto::FileSize whole,
    proto::FileOffset at
) {
    TASTY_SEAT_BODY(LadderHostAdapter);
    std::vector<std::uint8_t> buf(bytes.begin(), bytes.end());
    return edges_.inbox.intern_file(
        FileIntern{.bytes = std::move(buf), .ext = ext, .piece = {.whole = whole, .offset = at}}
    );
}

Ex<proto::FileId> LadderHostAdapter::intern_path(std::string_view path, proto::FileSize size) {
    TASTY_SEAT_BODY(LadderHostAdapter);
    auto id = edges_.inbox.intern_file_path(path, size);
    if (!id)
        return std::unexpected(id.error());
    ++edges_.tally.images_sized;
    return *id;
}

Ex<proto::FileId> LadderHostAdapter::intern_whole(
    std::vector<std::uint8_t>&& bytes,
    std::string_view path
) {
    TASTY_SEAT_BODY(LadderHostAdapter);
    return edges_.inbox.intern_file(
        FileIntern{.bytes = std::move(bytes), .ext = infra::extension(path), .path = path}
    );
}

cores::FileTxAnswer LadderHostAdapter::file_tx_answer(proto::ActKey act) {
    TASTY_SEAT_BODY(LadderHostAdapter);
    const FileTxLevel level = edges_.ftx_level_cell.sample().value;
    if (!act || level.stream_act != act)
        return cores::FileTxAnswer::Pending;
    return level.stream_served ? cores::FileTxAnswer::Served : cores::FileTxAnswer::Dropped;
}

cores::FileTxAnswer LadderHostAdapter::payload_answer(proto::ActKey16 act) {
    TASTY_SEAT_BODY(LadderHostAdapter);
    const FileTxLevel level = edges_.ftx_level_cell.sample().value;
    if (!act || !level.payload || level.act != act.widen())
        return cores::FileTxAnswer::Pending;
    return level.open ? cores::FileTxAnswer::Served : cores::FileTxAnswer::Dropped;
}

bool LadderHostAdapter::order_load_facts(std::span<const std::uint8_t> facts) {
    TASTY_SEAT_BODY(LadderHostAdapter);
    if (edges_.inbox.ring().size() >= proto::kLinkTxCapacity)
        return false;

    return edges_.inbox.push_load_facts(facts, {}).has_value();
}

std::size_t LadderHostAdapter::link_in_flight() const noexcept {
    TASTY_SEAT_BODY(LadderHostAdapter);
    return edges_.inbox.ring().size();
}

hal::Width LadderHostAdapter::link_width() const noexcept {
    TASTY_SEAT_BODY(LadderHostAdapter);
    return edges_.negotiated.link_width();
}

cores::MountStatus LadderHostAdapter::mount_status() {
    TASTY_SEAT_BODY(LadderHostAdapter);
    return edges_.mount_status_cell.sample().value;
}

bool LadderHostAdapter::order_walk(proto::IoIndex slot, std::string_view path, proto::ActKey gen) {
    TASTY_SEAT_BODY(LadderHostAdapter);
    if (edges_.belt.any_live(kWalkBlockers))
        return false;
    WalkAsk ask{.slot = slot, .gen = gen};
    if (walk_ || !ask.path.assign(path)) {
        ++edges_.tally.files_failed;
        return false;
    }
    walk_ = std::move(ask);
    return true;
}

cores::MountStatus LadderHostAdapter::walk_status() {
    TASTY_SEAT_BODY(LadderHostAdapter);
    return edges_.belt.walk_status();
}

bool LadderHostAdapter::order_savestates(std::string_view image, proto::ActKey gen) {
    TASTY_SEAT_BODY(LadderHostAdapter);
    const auto& conf = edges_.negotiated.conf_str();
    if (!conf || !conf->savestate().has_value())
        return false;
    const bool service = edges_.save_state_asks
        && edges_.save_state_facts
        && edges_.save_state_facts->generation().held();
    SaveStateAttach::Ask ask{
        .decl = *conf->savestate(),
        .core_name = edges_.negotiated.core_name(),
        .load_hint = edges_.negotiated.made_with_manifest() ? cores::LoadHint::XmlManifest
                                                            : cores::LoadHint::None,
        .act = gen
    };
    if (!service || !ask.rom_path.assign(image) || !edges_.save_state_asks->push(ask)) {
        ++edges_.tally.savestate_refusals;
        return false;
    }
    return true;
}

proto::ActKey LadderHostAdapter::savestates_answered() {
    TASTY_SEAT_BODY(LadderHostAdapter);
    return edges_.save_state_facts ? edges_.save_state_facts->sample().value.answered
                                   : proto::ActKey{};
}

cores::SaveExtent LadderHostAdapter::save_extent() {
    TASTY_SEAT_BODY(LadderHostAdapter);
    return edges_.save_extent_cell.sample().value;
}

proto::StatusWord LadderHostAdapter::status_word() {
    TASTY_SEAT_BODY(LadderHostAdapter);
    return edges_.status_cell.sample().value;
}

std::optional<proto::FieldValue> LadderHostAdapter::option_choice(cores::OptionTag tag) {
    TASTY_SEAT_BODY(LadderHostAdapter);
    const OptionTable table = edges_.option_cell.sample().value;
    const auto rows = std::span(table.rows).first(table.count);
    const auto row = std::ranges::find_if(rows, [tag](const OptionTable::Row& r) {
        return r.decl.has_value() && r.decl->id == tag;
    });
    if (row == rows.end())
        return std::nullopt;
    return proto::FieldValue{row->current};
}

std::span<const std::byte> LadderHostAdapter::config_value(std::span<std::byte> dst) {
    TASTY_SEAT_BODY(LadderHostAdapter);
    if (!edges_.config_value_cell)
        return {};
    const auto sample = edges_.config_value_cell->sample();
    if (!sample)
        return {};
    const std::span<const std::byte> value = sample.value.view();
    if (value.empty() || dst.size() < value.size())
        return {};
    std::ranges::copy(value, dst.begin());
    return dst.first(value.size());
}

bool LadderHostAdapter::boot_screen() {
    TASTY_SEAT_BODY(LadderHostAdapter);
    return edges_.config_cell.sample().value.bootscreen != 0;
}

bool LadderHostAdapter::start_nic(svc::NicLink link) {
    TASTY_SEAT_BODY(LadderHostAdapter);
    if (!edges_.nic)
        return true;
    const infra::OptRef<const cores::CoreFactory> row = edges_.negotiated.load_row();
    if (!row || !row->has_nic())
        return true;
    return edges_.nic->push(
        infra::make<svc::NicControl>(svc::NicControl::Start{.make = row->make_nic, .link = link})
    );
}

void LadderHostAdapter::game_identified(
    std::string_view image,
    const svc::disc::DiscIdText& serial
) {
    TASTY_SEAT_BODY(LadderHostAdapter);
    GameId id{};
    (void)id.file.assign(infra::basename(image));
    (void)id.serial.assign(serial.view());
    id.enabled = edges_.config_cell.sample().value.log_file_entry != 0;
    edges_.game_id_cell.publish(id);
}

void LadderHostAdapter::reset_share() {
    TASTY_SEAT_BODY(LadderHostAdapter);
    if (!edges_.share_grant || !edges_.vfs)
        return;
    const auto row = edges_.negotiated.load_row();
    if (!row || !row->profile.share || edges_.aperture.region.len == 0)
        return;
    const cores::ShareRow& share = *row->profile.share;
    ShareGrant::Open next = share_.value_or(ShareGrant::Open{});
    if (open_live_) {
        next.reset = next.reset.next();
    } else {
        next.open = next.open.next();
        next.reset = next.reset.next();
    }
    next.make = row->make_share_host;
    next.bytes = share.bytes;
    next.poll = share.poll;
    const auto page = share.page.address_in(row->profile.windows, edges_.aperture.region.phys);

    const std::string_view home_name = row->profile.home_dir.empty()
        ? edges_.negotiated.core_name().view()
        : row->profile.home_dir;

    const std::string home = StartChain::core_home(home_name, *edges_.vfs);
    const svc::ConfigSnapshot cfg = edges_.config_cell.sample().value;
    const cores::ShareRootRule rule{
        .home = home,
        .configured = cfg.shared_folder,
        .default_dir = share.default_dir,
        .home_name = home_name
    };
    const std::string root = cores::share_root(rule);
    if (!page || !next.root.assign(root)) {
        open_live_ = false;
        share_ = std::move(next);
        publish_closed_();
        return;
    }
    next.page = *page;
    ShareGrant grant;
    grant.want = next;
    edges_.share_grant->publish(grant);
    share_ = std::move(next);
    open_live_ = true;
    if (edges_.io_wake)
        edges_.io_wake->kick();
}

void LadderHostAdapter::close_share() noexcept {
    TASTY_SEAT_BODY(LadderHostAdapter);
    if (!open_live_)
        return;
    open_live_ = false;
    publish_closed_();
}

void LadderHostAdapter::publish_closed_() noexcept {
    if (!edges_.share_grant)
        return;
    edges_.share_grant->publish(ShareGrant{});
    if (edges_.io_wake)
        edges_.io_wake->kick();
}

}
