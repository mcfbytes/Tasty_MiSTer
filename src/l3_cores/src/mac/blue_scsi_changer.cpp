// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/mac/blue_scsi_changer.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <set>
#include <string>

#include "infra/ascii.h"
#include "cores/mac/blue_scsi_cdb.h"
#include "cores/mac/image_ext.h"
#include "infra/byte_view.h"

namespace mister::cores::mac {

namespace {

constexpr BlueScsiReply kCheck{.status = BlueScsiStatus::Check, .bytes = {}};

constexpr infra::ByteCount kCountAnswer{1};

constexpr std::size_t kSheetBytes = 65536;

struct OpRow {
    std::byte opcode;
    BlueScsiChanger::Op op;
};

constexpr std::array kOps{
    OpRow{std::byte{0xD7}, BlueScsiChanger::Op::List},
    OpRow{std::byte{0xD8}, BlueScsiChanger::Op::SetNext},
    OpRow{std::byte{0xDA}, BlueScsiChanger::Op::Count},
};

[[nodiscard]] std::unexpected<Error> unknown() noexcept {
    return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
}

[[nodiscard]] std::string lowered(std::string_view s) {
    std::string out(s);
    for (char& c : out)
        c = infra::ascii_lower(c);
    return out;
}

[[nodiscard]] bool mounts_directly(std::string_view name) noexcept {
    constexpr std::array kDirect{kIsoExt, kToastExt, kToaExt, kCueExt, kChdExt};
    return std::ranges::any_of(kDirect, [name](ImageExt ext) { return ext.names(name); });
}

void quoted_files(std::string_view sheet, std::set<std::string>& into) {
    while (!sheet.empty()) {
        const std::size_t eol = sheet.find('\n');
        std::string_view line = sheet.substr(0, eol);
        sheet = eol == std::string_view::npos ? std::string_view{} : sheet.substr(eol + 1);
        while (!line.empty() && (line.front() == ' ' || line.front() == '\t'))
            line.remove_prefix(1);
        if (!infra::istarts_with(line, "FILE"))
            continue;
        const std::size_t open = line.find('"');
        if (open == std::string_view::npos)
            continue;
        const std::size_t shut = line.find('"', open + 1);
        if (shut == std::string_view::npos)
            continue;
        std::string_view quoted = line.substr(open + 1, shut - open - 1);
        if (const std::size_t cut = quoted.find_last_of("/\\"); cut != std::string_view::npos)
            quoted.remove_prefix(cut + 1);
        into.insert(lowered(quoted));
    }
}

}

Ex<BlueScsiChanger::Request> BlueScsiChanger::decode(std::span<const std::byte> request) const {
    const auto cdb = BlueScsiCdb::of(request);
    if (!cdb)
        return unknown();
    const auto row = std::ranges::find(kOps, cdb->opcode(), &OpRow::opcode);
    if (row == kOps.end())
        return unknown();
    return Request{.op = row->op, .index = cdb->index()};
}

infra::ByteCount BlueScsiChanger::encode(
    const Reply& answered,
    std::span<std::byte> reply_out
) const {
    return blue_scsi_status_block(answered.status, answered.bytes, reply_out);
}

std::vector<svc::DirEntry> BlueScsiChanger::discs_(const HostFileSandbox& discs) const {
    auto found = discs.list();
    if (!found)
        return {};
    std::vector<svc::DirEntry> all = std::move(*found);
    std::erase_if(all, [](const svc::DirEntry& e) { return e.name.starts_with('.'); });
    std::set<std::string> owned;
    std::string sheet;
    for (const svc::DirEntry& e : all) {
        if (!kCueExt.names(e.name))
            continue;
        auto f = discs.open(e.name);
        if (!f)
            continue;
        sheet.assign(kSheetBytes, '\0');
        const infra::ByteCount got = (*f)->read_prefix_at(
                                             proto::kFileStart,
                                             infra::byte_cast<std::byte>(std::span<char>(sheet))
        )
                                         .value_or(infra::ByteCount{});
        got.resize_in(sheet);
        quoted_files(sheet, owned);
    }
    std::erase_if(all, [&owned](const svc::DirEntry& e) {
        if (mounts_directly(e.name))
            return false;
        return !kBinExt.names(e.name) || owned.contains(lowered(e.name));
    });
    std::ranges::sort(all, {}, &svc::DirEntry::name);
    if (all.size() > kBlueScsiChangerDiscs)
        all.resize(kBlueScsiChangerDiscs);
    return all;
}

Ex<BlueScsiChanger::Reply> BlueScsiChanger::serve(
    Request& asked,
    HostFileSandbox& discs,
    MailboxWindow& window
) {
    TASTY_SEAT_BODY(BlueScsiChanger);
    auto page = window.at(MailboxSpan{.off = {0}, .len = mailbox_len(window.size())});
    if (!page)
        return std::unexpected(page.error());
    const std::span<std::byte> out = *page;
    const std::vector<svc::DirEntry> names = discs_(discs);
    switch (asked.op) {
        case Op::Count: {
            if (out.empty())
                return kCheck;
            out.front() = ListingIndex::end_of(std::span<const svc::DirEntry>(names)).wire_byte();
            return Reply{.status = BlueScsiStatus::Good, .bytes = kCountAnswer};
        }
        case Op::List: {
            std::size_t at = 0;
            ListingIndex index{};
            for (const svc::DirEntry& e : names) {
                if (at + kBlueScsiEntryBytes > out.size())
                    break;
                const auto
                    entry = blue_scsi_entry(index, BlueScsiEntryKind::File, e.name, e.size, '_');
                std::memcpy(out.data() + at, entry.data(), entry.size());
                at += entry.size();
                index = index.next();
            }
            return Reply{.status = BlueScsiStatus::Good, .bytes = infra::ByteCount{at}};
        }
        case Op::SetNext: {
            const auto named = asked.index.in(std::span<const svc::DirEntry>(names));
            if (!named || !remount_)
                return kCheck;
            const auto path = discs.resolve(named->name);
            RemountAsked ask{.slot = disc_slot_};
            if (!path || !ask.path.assign(*path))
                return kCheck;
            remount_->publish(ask);
            return Reply{.status = BlueScsiStatus::Good, .bytes = {}};
        }
    }
    return kCheck;
}

}
