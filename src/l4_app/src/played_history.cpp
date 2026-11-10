// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/played_history.h"

#include <algorithm>
#include <cerrno>
#include <span>

#include "app/text_field.h"
#include "app/ui_write_lane.h"
#include "infra/byte_view.h"
#include "infra/endian.h"
#include "svc/vfs.h"

namespace mister::app {

namespace {

constexpr std::string_view kRel = "config/played.dat";

[[nodiscard]] bool no_file_yet(const Error& e) noexcept {
    return e.code == Errc::not_found || (e.code == Errc::os && e.detail == ENOENT);
}

}

Ex<void> PlayedHistory::load() {
    TASTY_SEAT_BODY(PlayedHistory);
    if (loaded_)
        return {};
    records_.clear();

    std::vector<std::byte> buf(kFileBytes, std::byte{0});
    auto f = vfs_.open(kRel, svc::OpenMode::ReadWhole);
    if (!f) {

        if (!no_file_yet(f.error())) {
            ++errors_;
            return std::unexpected(f.error());
        }
        loaded_ = true;
        return {};
    }
    std::size_t off = 0;
    while (off < buf.size()) {
        auto n = (*f)->read_at(off, std::span{buf}.subspan(off));
        if (!n) {
            ++errors_;
            return std::unexpected(n.error());
        }
        if (*n == 0)
            break;
        off += *n;
    }
    ++loads_;

    if (off < sizeof(Header)) {
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
    if (off < kHeaderBytes + count * sizeof(Stored)) {
        ++rejected_;
        loaded_ = true;
        return {};
    }

    records_.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        const auto s = infra::load_native<Stored>(std::span<const std::byte>(buf)
                                                      .subspan(kHeaderBytes + i * sizeof(Stored))
                                                      .first<sizeof(Stored)>());
        Record r;
        r.core_image.assign(TextField{std::span{s.core_image}}.text());
        r.core_name.assign(TextField{std::span{s.core_name}}.text());
        r.path.assign(TextField{std::span{s.path}}.text());
        r.title.assign(TextField{std::span{s.title}}.text());
        r.ioctl_index = proto::IoIndex{s.ioctl_index};
        r.slot = s.slot;
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
    if (records_.size() > kMax)
        records_.resize(kMax);
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
        rec[offsetof(Stored, ioctl_index)] = r.ioctl_index.record_byte();
        rec[offsetof(Stored, slot)] = r.slot;
    }
    const auto r = writes_.arm(kRel, buf, VerdictSink::Played);
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
