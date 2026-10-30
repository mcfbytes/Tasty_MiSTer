// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/ladder_host_adapter.h"

#include <utility>

#include "app/negotiated_core.h"
#include "app/owner_push.h"

namespace mister::app {

std::uint32_t LadderHostAdapter::next_generation() noexcept {
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
    std::uint32_t load_addr
) {
    TASTY_SEAT_BODY(LadderHostAdapter);
    std::vector<std::uint8_t> buf(bytes.begin(), bytes.end());
    auto id = edges_.inbox.intern_file(std::move(buf), ext, std::string_view{}, load_addr);
    if (!id)
        return std::unexpected(id.error());
    return *id;
}

Ex<proto::FileId> LadderHostAdapter::intern_piece(
    std::span<const std::uint8_t> bytes,
    std::string_view ext,
    std::uint64_t whole,
    std::uint64_t offset
) {
    TASTY_SEAT_BODY(LadderHostAdapter);
    std::vector<std::uint8_t> buf(bytes.begin(), bytes.end());
    return edges_.inbox.intern_file(std::move(buf), ext, std::string_view{}, 0, 0, whole, offset);
}

Ex<proto::FileId> LadderHostAdapter::intern_path(std::string_view path, std::uint64_t size_bytes) {
    TASTY_SEAT_BODY(LadderHostAdapter);
    auto id = edges_.inbox.intern_file_path(path, size_bytes);
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
    const std::size_t dot = path.rfind('.');
    const std::string_view ext = dot == std::string_view::npos ? std::string_view{}
                                                               : path.substr(dot);
    return edges_.inbox.intern_file(std::move(bytes), ext, path, 0);
}

std::optional<bool> LadderHostAdapter::file_tx_answer(std::uint32_t act) {
    TASTY_SEAT_BODY(LadderHostAdapter);
    const FileTxLevel level = edges_.ftx_level_cell.sample().value;
    if (act == 0 || level.stream_act != act)
        return std::nullopt;
    return level.stream_served;
}

bool LadderHostAdapter::order_load_facts(std::span<const std::uint8_t> facts) {
    TASTY_SEAT_BODY(LadderHostAdapter);
    if (edges_.inbox.ring().size() >= proto::kLinkTxCapacity)
        return false;
    const auto slab = edges_.inbox.intern(facts);
    if (!slab)
        return false;
    return order(proto::LinkOp::LoadFacts{.facts = *slab});
}

std::size_t LadderHostAdapter::link_in_flight() const noexcept {
    TASTY_SEAT_BODY(LadderHostAdapter);
    return edges_.inbox.ring().size();
}

std::size_t LadderHostAdapter::link_word_bytes() const noexcept {
    TASTY_SEAT_BODY(LadderHostAdapter);
    return edges_.negotiated.link_wide() ? 2u : 1u;
}

cores::MountStatus LadderHostAdapter::mount_status() {
    TASTY_SEAT_BODY(LadderHostAdapter);
    return edges_.mount_status_cell.sample().value;
}

bool LadderHostAdapter::order_walk(proto::IoIndex slot, std::string_view path, std::uint32_t gen) {
    TASTY_SEAT_BODY(LadderHostAdapter);
    return acts_.order_walk(slot, path, gen);
}

cores::MountStatus LadderHostAdapter::walk_status() {
    TASTY_SEAT_BODY(LadderHostAdapter);
    return edges_.belt.walk_status();
}

bool LadderHostAdapter::order_savestates(std::string_view image, std::uint32_t gen) {
    TASTY_SEAT_BODY(LadderHostAdapter);
    return acts_.order_savestates(image, gen);
}

std::uint32_t LadderHostAdapter::savestates_answered() {
    TASTY_SEAT_BODY(LadderHostAdapter);
    return edges_.save_state_facts ? edges_.save_state_facts->sample().value.answered : 0u;
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
