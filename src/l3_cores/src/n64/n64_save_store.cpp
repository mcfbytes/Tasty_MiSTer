// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/n64/n64_save_store.h"

#include <algorithm>
#include <cstring>
#include <utility>

#include "cores/n64/n64_cpak.h"
#include "cores/n64/n64_save_attach.h"
#include "cores/n64/n64_save_layout_key.h"
#include "infra/byte_view.h"
#include "infra/prelude.h"

namespace mister::cores::n64 {

SaveChannelStore::SaveChannelStore(
    const svc::Vfs& vfs,
    infra::OptRef<xthread::WakeFlag> activity
) noexcept
    : vfs_(vfs),
      card_(vfs),
      activity_(activity) {}

bool SaveChannelStore::all_blank_(std::span<const std::uint8_t> image) const noexcept {
    if (image.empty())
        return true;
    const std::uint8_t head = image[0];
    if (head != 0x00 && head != 0xFF)
        return false;
    for (std::size_t i = 1; i < image.size(); ++i) {
        if (image[i] != head)
            return false;
    }
    return true;
}

void SaveChannelStore::adopt_(const SavePlan& plan) noexcept {
    plan_ = plan;
    layout_ = plan_.layout();
    key_ = proto::ImageTag{};
}

void SaveChannelStore::note_tag(proto::ImageTag tag) noexcept {
    TASTY_SEAT_BODY(SaveChannelStore);
    if (layout_.count.none() || tag == key_)
        return;
    const SaveLayout live = expand_save_layout(layout_key_from_tag(tag));
    if (!live.valid || live.count != layout_.count) {
        ++layout_errors_;
        return;
    }
    layout_ = live;
    key_ = tag;
}

Ex<std::size_t> SaveChannelStore::render_(const SaveOrder& o) {
    const std::size_t sz = o.bytes;
    std::memset(blank_, 0, sz);
    if (migrate_(o))
        return sz;
    switch (o.create) {
        case CreateImage::ControllerPak: {
            auto serial = cpak::random_serial();
            if (!serial)
                return std::unexpected(serial.error());
            cpak::render(
                *serial,
                std::span<std::uint8_t, cpak::kImageBytes>(blank_, cpak::kImageBytes)
            );
            return sz;
        }
        case CreateImage::Blank:
            break;
        case CreateImage::Never:
            std::unreachable();
    }
    std::memset(blank_, 0xFF, sz);
    return sz;
}

bool SaveChannelStore::migrate_(const SaveOrder& o) {
    const std::string_view old = plan_.stock.view();
    if (old.empty() || !vfs_.file_exists(old))
        return false;
    auto f = vfs_.open(old, svc::OpenMode::ReadWhole);
    if (!f)
        return false;
    auto on_disk = (*f)->size();
    if (!on_disk)
        return false;
    const std::uint64_t end = std::min<std::uint64_t>(
        on_disk->file_word<std::uint64_t>(),
        kMaxPartitionBytes
    );
    if (o.offset >= end)
        return false;
    const std::size_t sz = o.bytes;
    const auto want = static_cast<std::size_t>(std::min<std::uint64_t>(sz, end - o.offset));
    const proto::FileOffset from{o.offset};
    auto got = (*f)->read_at(from, std::as_writable_bytes(std::span<std::uint8_t>(blank_, want)));
    if (!got || got->empty() || all_blank_(std::span<const std::uint8_t>(blank_, sz)))
        return false;
    const std::span<std::uint8_t> slot(blank_, sz);
    if (plan_.rows[o.row].byteswap)
        word_reverse_.normalize(got->first_in(slot));
    std::ranges::fill(got->past_in(slot), std::uint8_t{0xFF});
    ++migrated_;
    return true;
}

Ex<void> SaveChannelStore::create_(const SaveOrder& o, std::size_t bytes) {
    const std::span<const std::byte> image = std::as_bytes(
        std::span<const std::uint8_t>(blank_, bytes)
    );
    if (auto r = card_.replace_file(o.path.view(), image); !r)
        return r;
    ++creates_;
    return {};
}

Ex<proto::FileSize> SaveChannelStore::open_extent(
    proto::SlotIndex slot,
    const svc::BlockCtl::StorageOrder& order
) {
    TASTY_SEAT_BODY(SaveChannelStore);

    release(slot);
    const auto attach = SaveAttach::decode(order.view());
    if (!attach)
        return std::unexpected(slot.refusal(Errc::mount_failed, ERR_SITE()));
    const SaveOrder& o = attach->order;

    if (o.bytes > kMaxPartitionBytes)
        return std::unexpected(o.row.refusal(Errc::mount_failed, ERR_SITE()));
    const auto part = parts_.find(o.row);
    if (!part)
        return std::unexpected(o.row.refusal(Errc::slot_range, ERR_SITE()));
    part->claim(slot);

    part->close();
    if (attach->plan)
        adopt_(*attach->plan);
    if (o.path.view().empty()) {
        return std::unexpected(o.row.refusal(Errc::mount_failed, ERR_SITE()));
    }
    if (o.create != CreateImage::Never && !vfs_.file_exists(o.path.view())) {
        auto n = render_(o);
        if (!n)
            return std::unexpected(n.error());
        if (auto r = create_(o, *n); !r)
            return std::unexpected(r.error());
    }
    return part->open(card_, o.path.view());
}

void SaveChannelStore::release(proto::SlotIndex slot) noexcept {
    TASTY_SEAT_BODY(SaveChannelStore);
    for (OpenPartition& part : parts_) {
        if (part.claimed_by(slot))
            part.release();
    }
}

Ex<proto::FileSize> SaveChannelStore::OpenPartition::open(
    svc::CardWriter& card,
    std::string_view path
) {
    auto f = card.open(path, svc::OpenMode::ReadWrite);
    if (!f)
        return std::unexpected(f.error());
    auto size = (*f)->size();
    if (!size)
        return std::unexpected(size.error());
    file_ = std::move(*f);
    on_disk_ = *size;
    return on_disk_;
}

Ex<infra::ByteCount> SaveChannelStore::read_at(proto::FileOffset at, std::span<std::byte> dst) {
    TASTY_SEAT_BODY(SaveChannelStore);
    const std::uint64_t offset = (at - proto::kFileStart).file_word<std::uint64_t>();
    if (layout_.count.none()) {
        ++stray_;
        return infra::ByteCount{0};
    }
    bool noted = false;
    std::size_t done = 0;
    while (done < dst.size()) {
        auto hit = layout_.locate(offset + done);
        if (!hit)
            break;
        const auto room = static_cast<std::size_t>(hit->rest.length);
        const std::size_t want = std::min(dst.size() - done, room);
        const auto f = parts_[hit->row].file();
        if (!f)
            break;
        if (!noted) {
            if (activity_)
                activity_->request();
            noted = true;
        }
        const proto::FileOffset row_at{hit->rest.offset};
        const std::span<std::byte> slot = dst.subspan(done, want);
        auto got = f->read_at(row_at, slot);
        if (!got)
            break;
        if (plan_.rows[hit->row].byteswap) {
            word_reverse_.normalize(infra::byte_cast<std::uint8_t>(got->first_in(slot)));
        }

        got->zero_past_in(slot);
        done += want;
    }
    return infra::ByteCount{done};
}

Ex<infra::ByteCount> SaveChannelStore::write_at(
    proto::FileOffset at,
    std::span<const std::byte> src
) {
    TASTY_SEAT_BODY(SaveChannelStore);
    const std::uint64_t offset = (at - proto::kFileStart).file_word<std::uint64_t>();
    if (layout_.count.none()) {
        ++stray_;
        return infra::ByteCount{0};
    }

    if (auto head = layout_.locate(offset); head
        && parts_[head->row].is_open()
        && !parts_[head->row].on_disk().holds(proto::FileOffset{head->rest.offset})) {
        ++clamped_;
        return infra::ByteCount{src.size()};
    }
    bool noted = false;
    std::size_t done = 0;
    while (done < src.size()) {
        auto hit = layout_.locate(offset + done);
        if (!hit)
            break;
        const auto room = static_cast<std::size_t>(hit->rest.length);
        const std::size_t want = std::min({src.size() - done, room, kMaxSliceBytes});
        OpenPartition& part = parts_[hit->row];
        const auto f = part.file();
        if (!f)
            break;

        const proto::FileOffset row_at{hit->rest.offset};
        const std::size_t fits = part.on_disk().clamp_write(row_at, want);
        if (fits != want)
            ++clamped_;
        if (fits == 0) {
            done += want;
            continue;
        }
        if (!noted) {
            if (activity_)
                activity_->request();
            noted = true;
        }

        std::memcpy(scratch_, src.data() + done, fits);
        if (plan_.rows[hit->row].byteswap) {
            word_reverse_.normalize(std::span<std::uint8_t>(scratch_, fits));
        }

        auto existing = f->read_at(
            row_at,
            infra::byte_cast<std::byte>(std::span<std::uint8_t>(compare_, fits))
        );
        if (existing
            && *existing == infra::ByteCount{fits}
            && std::memcmp(compare_, scratch_, fits) == 0) {
            ++unchanged_;
            done += want;
            continue;
        }
        auto n = f->write_at(
            row_at,
            infra::byte_cast<const std::byte>(std::span<const std::uint8_t>(scratch_, fits))
        );
        if (!n || *n != infra::ByteCount{fits})
            break;

        if (auto fl = f->flush(); !fl)
            break;
        done += want;
    }
    return infra::ByteCount{done};
}

}
