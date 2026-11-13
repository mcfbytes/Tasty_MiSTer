// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/ladder_host_adapter.h"

#include <utility>

#include "app/negotiated_core.h"
#include "app/owner_push.h"
#include "infra/path_parts.h"

namespace mister::app {

proto::ActKey LadderHostAdapter::next_generation() noexcept {
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
    std::uint64_t offset
) {
    TASTY_SEAT_BODY(LadderHostAdapter);
    std::vector<std::uint8_t> buf(bytes.begin(), bytes.end());
    return edges_.inbox.intern_file(
        FileIntern{
            .bytes = std::move(buf),
            .ext = ext,
            .piece = {.whole = whole, .offset = proto::FileSize{offset}}
        }
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

cores::WireWordBytes LadderHostAdapter::link_word_bytes() const noexcept {
    TASTY_SEAT_BODY(LadderHostAdapter);
    return cores::WireWordBytes::of(
        edges_.negotiated.link_wide() ? hal::Width::Word : hal::Width::Byte
    );
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
        && edges_.save_state_facts->generation() != 0;
    SaveStateAttach::Ask ask{
        .decl = *conf->savestate(),
        .core_name = edges_.negotiated.core_name(),
        .load_hint = edges_.negotiated.made_with_manifest() ? cores::LoadHint::XmlManifest
                                                            : cores::LoadHint::None,
        .generation = gen
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

}
