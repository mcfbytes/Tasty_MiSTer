// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/mac/blue_scsi_toolbox.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <string>

#include "cores/mac/blue_scsi_cdb.h"

namespace mister::cores::mac {

namespace {

constexpr BlueScsiReply kCheck{.status = BlueScsiStatus::Check, .bytes = {}};

constexpr infra::ByteCount kCountAnswer{1};
constexpr BlueScsiReply kGood{.status = BlueScsiStatus::Good, .bytes = {}};

struct OpRow {
    std::byte opcode;
    BlueScsiToolbox::Op op;
};

constexpr std::array kOps{
    OpRow{std::byte{0xD0}, BlueScsiToolbox::Op::List},
    OpRow{std::byte{0xD1}, BlueScsiToolbox::Op::Get},
    OpRow{std::byte{0xD2}, BlueScsiToolbox::Op::Count},
    OpRow{std::byte{0xD3}, BlueScsiToolbox::Op::SendPrep},
    OpRow{std::byte{0xD4}, BlueScsiToolbox::Op::SendData},
    OpRow{std::byte{0xD5}, BlueScsiToolbox::Op::SendEnd},
};

constexpr proto::BlockSize kGetBlock{4096};
constexpr proto::BlockCount kOneBlock{1};

[[nodiscard]] std::unexpected<Error> unknown() noexcept {
    return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
}

[[nodiscard]] bool name_safe(std::string_view name) noexcept {
    return !name.empty()
        && name.find('/') == std::string_view::npos
        && name.find("..") == std::string_view::npos;
}

[[nodiscard]] std::string upload_name(std::span<const std::byte> payload) {
    std::string name;
    const std::span<const std::byte> field = payload.first(
        std::min(payload.size(), kBlueScsiNameBytes)
    );
    for (const std::byte b : field) {
        if (b == std::byte{})
            break;
        name.push_back(blue_scsi_ascii(b, '?'));
    }
    return name;
}

}

Ex<BlueScsiToolbox::Request> BlueScsiToolbox::decode(std::span<const std::byte> request) const {
    const auto cdb = BlueScsiCdb::of(request);
    if (!cdb)
        return unknown();
    const auto row = std::ranges::find(kOps, cdb->opcode(), &OpRow::opcode);
    if (row == kOps.end())
        return unknown();
    Request r{.op = row->op};
    switch (r.op) {
        case Op::Get: {
            const proto::BlockCount blocks = cdb->blocks();
            r.index = cdb->index();
            r.at = kGetBlock.position_of(cdb->wide_block());
            r.want = infra::ByteCount{std::min<std::size_t>(
                kGetBlock.window_bytes(blocks == proto::BlockCount{} ? kOneBlock : blocks),
                kBlueScsiChunkBytes
            )};
            break;
        }
        case Op::SendPrep:
            r.head = cdb->payload();
            break;
        case Op::SendData: {

            const proto::BlockCount blocks = cdb->blocks();
            const std::size_t valid = std::min<std::size_t>(
                blocks == proto::BlockCount{} ? cdb->byte_count().size()
                                              : kBlueScsiBlock.window_bytes(blocks),
                kBlueScsiChunkBytes
            );
            r.at = kBlueScsiBlock.position_of(cdb->block());
            r.head = cdb->payload().first(std::min(valid, kBlueScsiHeadPayload));
            if (valid - r.head.size() > cdb->tail().size())
                return unknown();
            r.tail = cdb->tail().first(valid - r.head.size());
            break;
        }
        case Op::List:
        case Op::Count:
        case Op::SendEnd:
            break;
    }
    return r;
}

infra::ByteCount BlueScsiToolbox::encode(
    const Reply& answered,
    std::span<std::byte> reply_out
) const {
    return blue_scsi_status_block(answered.status, answered.bytes, reply_out);
}

Ex<BlueScsiToolbox::Reply> BlueScsiToolbox::serve(
    Request& asked,
    HostFileSandbox& share,
    MailboxWindow& window
) {
    TASTY_SEAT_BODY(BlueScsiToolbox);
    auto out = window.at(MailboxSpan{.off = {0}, .len = mailbox_len(window.size())});
    if (!out)
        return std::unexpected(out.error());
    switch (asked.op) {
        case Op::Count:
            return count_(share, *out);
        case Op::List:
            return list_(share, *out);
        case Op::Get:
            return get_(asked, share, *out);
        case Op::SendPrep:
            return send_prep_(asked, share);
        case Op::SendData:
            return send_data_(asked);
        case Op::SendEnd:
            return send_end_();
    }
    return kCheck;
}

void BlueScsiToolbox::close() noexcept {
    TASTY_SEAT_BODY(BlueScsiToolbox);
    (void)close_();
}

bool BlueScsiToolbox::close_() {
    if (!file_)
        return true;
    const bool kept = !uploading_ || file_->flush().has_value();
    file_.reset();
    uploading_ = false;
    return kept;
}

std::vector<svc::DirEntry> BlueScsiToolbox::names_(const HostFileSandbox& share) const {
    auto found = share.list();
    if (!found)
        return {};
    std::vector<svc::DirEntry> names = std::move(*found);
    std::erase_if(names, [](const svc::DirEntry& e) { return e.name.starts_with('.'); });
    std::ranges::sort(names, {}, &svc::DirEntry::name);
    if (names.size() > kBlueScsiShareNames)
        names.resize(kBlueScsiShareNames);
    return names;
}

BlueScsiToolbox::Reply BlueScsiToolbox::count_(
    const HostFileSandbox& share,
    std::span<std::byte> out
) {
    if (!share.ready() || out.empty())
        return kCheck;
    const std::vector<svc::DirEntry> names = names_(share);
    out.front() = ListingIndex::end_of(std::span<const svc::DirEntry>(names)).wire_byte();
    return Reply{.status = BlueScsiStatus::Good, .bytes = kCountAnswer};
}

BlueScsiToolbox::Reply BlueScsiToolbox::list_(
    const HostFileSandbox& share,
    std::span<std::byte> out
) {
    if (!share.ready())
        return kCheck;
    std::size_t at = 0;
    ListingIndex index{};
    for (const svc::DirEntry& e : names_(share)) {
        if (at + kBlueScsiEntryBytes > out.size())
            break;
        const auto entry = blue_scsi_entry(
            index,
            e.is_dir ? BlueScsiEntryKind::Directory : BlueScsiEntryKind::File,
            e.name,
            e.size,
            '?'
        );
        std::memcpy(out.data() + at, entry.data(), entry.size());
        at += entry.size();
        index = index.next();
    }
    return Reply{.status = BlueScsiStatus::Good, .bytes = infra::ByteCount{at}};
}

BlueScsiToolbox::Reply BlueScsiToolbox::get_(
    const Request& asked,
    const HostFileSandbox& share,
    std::span<std::byte> out
) {
    if (asked.at == proto::kFileStart) {
        (void)close_();
        const std::vector<svc::DirEntry> names = names_(share);
        if (const auto named = asked.index.in(std::span<const svc::DirEntry>(names))) {
            if (auto f = share.open(named->name))
                file_ = std::move(*f);
        }
    }
    if (!file_)
        return kCheck;
    if (uploading_) {

        (void)close_();
        return kGood;
    }
    const std::span<std::byte> into = out.first(std::min(asked.want.size(), out.size()));
    const infra::ByteCount got = file_->read_prefix_at(asked.at, into).value_or(infra::ByteCount{});
    if (got.empty())
        (void)close_();
    return Reply{.status = BlueScsiStatus::Good, .bytes = got};
}

BlueScsiToolbox::Reply BlueScsiToolbox::send_prep_(const Request& asked, HostFileSandbox& share) {
    if (!share.ready())
        return kCheck;
    (void)close_();
    const std::string name = upload_name(asked.head);
    if (!name_safe(name))
        return kCheck;
    auto f = share.create(name);
    if (!f)
        return kCheck;
    file_ = std::move(*f);
    uploading_ = true;
    return kGood;
}

BlueScsiToolbox::Reply BlueScsiToolbox::send_data_(const Request& asked) {
    if (!file_)
        return kCheck;
    chunk_.assign(asked.head.begin(), asked.head.end());
    chunk_.insert(chunk_.end(), asked.tail.begin(), asked.tail.end());
    if (!file_->write_all_at(asked.at, chunk_))
        return kCheck;
    return kGood;
}

BlueScsiToolbox::Reply BlueScsiToolbox::send_end_() {
    if (!file_)
        return kCheck;
    return close_() ? kGood : kCheck;
}

}
