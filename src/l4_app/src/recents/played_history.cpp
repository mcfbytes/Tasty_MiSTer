// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/recents/played_history.h"
#include "infra/span_cut.h"

#include <algorithm>
#include <optional>
#include <span>

#include "app/recents/text_field.h"
#include "app/recents/ui_write_lane.h"
#include "infra/byte_view.h"
#include "infra/endian.h"
#include "infra/prelude.h"
#include "svc/card_dirs.h"
#include "svc/vfs.h"

namespace mister::app::recents {

namespace {

constexpr std::string_view kRelName = "played.dat";

[[nodiscard]] bool no_file_yet(const Error& e) noexcept {
    return e.code == Errc::not_found;
}

constexpr char kFileLetter = 'F';
constexpr char kMountLetter = 'S';

[[nodiscard]] constexpr char letter_of(proto::FileRowKind kind) noexcept {
    switch (kind) {
        case proto::FileRowKind::File:
            return kFileLetter;
        case proto::FileRowKind::Mount:
            return kMountLetter;
    }
    return kFileLetter;
}

[[nodiscard]] constexpr std::optional<proto::FileRowKind> kind_of_letter(char letter) noexcept {
    switch (letter) {
        case kFileLetter:
            return proto::FileRowKind::File;
        case kMountLetter:
            return proto::FileRowKind::Mount;
        default:
            return std::nullopt;
    }
}

}

Ex<void> PlayedHistory::load() {
    TASTY_SEAT_BODY(PlayedHistory);
    if (loaded_)
        return {};
    records_.clear();

    std::vector<std::byte> buf(kFileBytes, std::byte{0});
    auto f = vfs_.open(svc::card_rel({svc::kConfigDir, kRelName}), svc::OpenMode::ReadWhole);
    if (!f) {

        if (!no_file_yet(f.error())) {
            ++errors_;
            return std::unexpected(f.error());
        }
        loaded_ = true;
        return {};
    }
    proto::FileOffset at = proto::kFileStart;
    const proto::FileSize total = proto::FileSize::of(std::span{buf});
    while (total.holds(at)) {
        auto n = (*f)->read_at(at, infra::affine::from(std::span{buf}, at));
        if (!n) {
            ++errors_;
            return std::unexpected(n.error());
        }
        if (n->empty())
            break;
        at += proto::FileSize::of(*n);
    }
    ++loads_;
    const proto::FileSize got = at - proto::kFileStart;

    if (got < proto::FileSize{sizeof(Header)}) {
        ++rejected_;
        loaded_ = true;
        return {};
    }
    const auto h = infra::load_native<Header>(
        std::span<const std::byte>(buf).first<sizeof(Header)>()
    );
    if (h.magic != kMagic || h.version != kVersion) {
        ++rejected_;
        loaded_ = true;
        return {};
    }
    if (h.count > kMax)
        clamped_ += static_cast<std::uint32_t>(h.count - kMax);
    const std::size_t count = std::min<std::size_t>(h.count, kMax);
    if (got < proto::FileSize{kHeaderBytes + count * sizeof(Stored)}) {
        ++rejected_;
        loaded_ = true;
        return {};
    }

    records_.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        const auto s = infra::load_native<Stored>(std::span<const std::byte>(buf)
                                                      .subspan(kHeaderBytes + i * sizeof(Stored))
                                                      .first<sizeof(Stored)>());
        const std::optional<proto::FileRowKind> kind = kind_of_letter(s.slot);
        if (!kind) {
            ++refusals_;
            continue;
        }
        Record r{
            .core_image = {},
            .core_name = {},
            .path = {},
            .title = {},
            .row = proto::FileRowId::of(*kind, proto::IoIndex{s.ioctl_index})
        };
        r.core_image.assign(TextField{std::span{s.core_image}}.text());
        r.core_name.assign(TextField{std::span{s.core_name}}.text());
        r.path.assign(TextField{std::span{s.path}}.text());
        r.title.assign(TextField{std::span{s.title}}.text());
        r.unix_time = s.unix_time;
        if (!names_a_game(r) || !fields_fit(r)) {
            ++refusals_;
            continue;
        }
        records_.push_back(static_cast<Record&&>(r));
    }
    loaded_ = true;
    return {};
}

Ex<void> PlayedHistory::record(const Record& r) {
    TASTY_SEAT_BODY(PlayedHistory);
    if (!names_a_game(r) || !fields_fit(r)) {
        ++refusals_;
        return {};
    }

    if (!loaded_) {
        if (auto l = load(); !l)
            return l;
    }
    const auto at = std::find_if(records_.begin(), records_.end(), [&r](const Record& have) {
        return have.core_image == r.core_image && have.path == r.path;
    });
    if (at != records_.end())
        records_.erase(at);
    records_.insert(records_.begin(), r);
    while (records_.size() > kMax)
        records_.pop_back();
    return save_();
}

Ex<void> PlayedHistory::save_() {
    std::vector<std::byte> buf(kHeaderBytes + records_.size() * kRecordBytes, std::byte{0});
    const Header h{kMagic, kVersion, static_cast<std::uint16_t>(records_.size())};
    infra::store_native<Header>(std::span<std::byte>(buf).first<sizeof(Header)>(), h);
    for (std::size_t i = 0; i < records_.size(); ++i) {
        const Record& r = records_[i];
        char* rec = infra::byte_cast<char>(std::span(buf)).data() + kHeaderBytes + i * kRecordBytes;
        infra::store_native<std::int64_t>(
            std::span<std::byte>(buf)
                .subspan(kHeaderBytes + i * kRecordBytes + offsetof(Stored, unix_time))
                .first<8>(),
            r.unix_time
        );
        const auto record = std::span<char, kRecordBytes>{rec, kRecordBytes};
        TextField{record.subspan<offsetof(Stored, core_image), kCoreImageCap>()}.assign(
            r.core_image
        );
        TextField{record.subspan<offsetof(Stored, core_name), kCoreNameCap>()}.assign(r.core_name);
        TextField{record.subspan<offsetof(Stored, path), kPathCap>()}.assign(r.path);
        TextField{record.subspan<offsetof(Stored, title), kTitleCap>()}.assign(r.title);
        rec[offsetof(Stored, ioctl_index)] = r.row.index.record_byte();
        rec[offsetof(Stored, slot)] = letter_of(r.row.kind);
    }
    const auto r = writes_
                       .arm(svc::card_rel({svc::kConfigDir, kRelName}), buf, VerdictSink::Played);
    if (!r)
        ++errors_;
    return r;
}

void PlayedHistory::on_written(bool durable) noexcept {
    TASTY_SEAT_BODY(PlayedHistory);
    if (durable) {
        ++saves_;
    } else {
        ++errors_;
    }
}

}
