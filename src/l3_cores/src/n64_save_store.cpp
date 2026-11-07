// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/n64_save_store.h"

#include <algorithm>
#include <cstring>
#include <optional>
#include <utility>

#include "cores/n64_cpak.h"
#include "cores/n64_save_attach.h"
#include "cores/n64_save_layout_key.h"
#include "cores/n64_rom.h"

namespace mister::cores::n64 {
namespace {

bool all_blank(const std::uint8_t* p, std::size_t n) noexcept {
    if (n == 0)
        return true;
    const std::uint8_t head = p[0];
    if (head != 0x00 && head != 0xFF)
        return false;
    for (std::size_t i = 1; i < n; ++i) {
        if (p[i] != head)
            return false;
    }
    return true;
}

}

SaveChannelStore::SaveChannelStore(const svc::Vfs& vfs) noexcept : vfs_(&vfs) {}

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
    const std::size_t sz = o.create;
    if (sz == 0 || sz > sizeof blank_) {
        return std::unexpected(Error{Errc::slot_range, ERR_SITE(), o.create});
    }
    std::memset(blank_, 0, sz);
    if (migrate_(o))
        return sz;
    if (o.cpak && sz == cpak::kImageBytes) {
        auto serial = cpak::random_serial();
        if (!serial)
            return std::unexpected(serial.error());
        cpak::render(*serial, std::span<std::uint8_t, cpak::kImageBytes>(blank_, sz));
        return sz;
    }
    std::memset(blank_, 0xFF, sz);
    return sz;
}

bool SaveChannelStore::migrate_(const SaveOrder& o) {
    const std::string_view old = plan_.stock.view();
    if (old.empty() || !vfs_->file_exists(old))
        return false;
    auto f = vfs_->open(old, svc::OpenMode::ReadWhole);
    if (!f)
        return false;
    auto on_disk = (*f)->size();
    if (!on_disk)
        return false;
    const std::uint64_t end = std::min<std::uint64_t>(on_disk->v, kMaxPartitionBytes);
    if (o.offset >= end)
        return false;
    const std::size_t sz = o.create;
    const auto want = static_cast<std::size_t>(std::min<std::uint64_t>(sz, end - o.offset));
    auto got = (*f)->read_at(
        o.offset,
        std::as_writable_bytes(std::span<std::uint8_t>(blank_, want))
    );
    if (!got || *got == 0 || all_blank(blank_, sz))
        return false;
    if (plan_.rows[o.row].byteswap)
        normalize(std::span<std::uint8_t>(blank_, *got), ByteOrder::Little);
    if (*got < sz)
        std::memset(blank_ + *got, 0xFF, sz - *got);
    ++migrated_;
    return true;
}

Ex<void> SaveChannelStore::create_(const SaveOrder& o, std::size_t bytes) {
    const std::string_view rel = o.path.view();
    if (const std::size_t slash = rel.rfind('/'); slash != std::string_view::npos && slash > 0) {
        if (auto r = vfs_->ensure_dir(rel.substr(0, slash)); !r)
            return r;
    }
    tmp_.clear();
    if (!tmp_.assign(rel) || !tmp_.append(".tmp")) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    }
    auto f = vfs_->open(tmp_.view(), svc::OpenMode::Truncate);
    if (!f)
        return std::unexpected(f.error());
    const std::span<const std::byte> image = std::as_bytes(
        std::span<const std::uint8_t>(blank_, bytes)
    );
    std::size_t off = 0;
    while (off < image.size()) {
        auto n = (*f)->write_at(off, image.subspan(off));
        if (!n)
            return std::unexpected(n.error());
        if (*n == 0) {
            return std::unexpected(
                Error{Errc::short_write, ERR_SITE(), static_cast<std::uint32_t>(off)}
            );
        }
        off += *n;
    }
    if (auto r = (*f)->flush(); !r)
        return r;
    f->reset();
    if (auto r = vfs_->replace(tmp_.view(), rel); !r)
        return r;
    ++creates_;
    return {};
}

Ex<proto::FileSize> SaveChannelStore::open_extent(
    proto::SlotIndex slot,
    std::span<const std::byte> order
) {
    TASTY_SEAT_BODY(SaveChannelStore);

    release(slot);
    const auto attach = SaveAttach::decode(order);
    if (!attach)
        return std::unexpected(Error{Errc::mount_failed, ERR_SITE(), slot.v});
    const SaveOrder& o = attach->order;
    const auto part = parts_.find(o.row);
    if (!part)
        return std::unexpected(Error{Errc::slot_range, ERR_SITE(), o.row.v});
    part->claim(slot);

    part->close();
    if (attach->plan)
        adopt_(*attach->plan);
    if (o.path.view().empty()) {
        return std::unexpected(Error{Errc::mount_failed, ERR_SITE(), o.row.v});
    }
    if (o.create != 0 && !vfs_->file_exists(o.path.view())) {
        auto n = render_(o);
        if (!n)
            return std::unexpected(n.error());
        if (auto r = create_(o, *n); !r)
            return std::unexpected(r.error());
    }
    return part->open(*vfs_, o.path.view());
}

void SaveChannelStore::release(proto::SlotIndex slot) noexcept {
    TASTY_SEAT_BODY(SaveChannelStore);
    for (OpenPartition& part : parts_) {
        if (part.claimed_by(slot))
            part.release();
    }
}

Ex<proto::FileSize> SaveChannelStore::OpenPartition::open(
    const svc::Vfs& vfs,
    std::string_view path
) {
    auto f = vfs.open(path, svc::OpenMode::ReadWrite);
    if (!f)
        return std::unexpected(f.error());
    auto size = (*f)->size();
    if (!size)
        return std::unexpected(size.error());
    file_ = std::move(*f);
    on_disk_ = *size;
    return on_disk_;
}

Ex<std::size_t> SaveChannelStore::read_at(std::uint64_t offset, std::span<std::uint8_t> dst) {
    TASTY_SEAT_BODY(SaveChannelStore);
    if (layout_.count.none()) {
        ++stray_;
        return std::size_t{0};
    }
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
        auto got = f->read_at(hit->rest.offset, std::as_writable_bytes(dst.subspan(done, want)));
        if (!got)
            break;
        if (plan_.rows[hit->row].byteswap) {
            normalize(dst.subspan(done, *got), ByteOrder::Little);
        }

        if (*got < want)
            std::memset(dst.data() + done + *got, 0, want - *got);
        done += want;
    }
    return done;
}

Ex<std::size_t> SaveChannelStore::write_at(
    std::uint64_t offset,
    std::span<const std::uint8_t> src
) {
    TASTY_SEAT_BODY(SaveChannelStore);
    if (layout_.count.none()) {
        ++stray_;
        return std::size_t{0};
    }

    if (auto head = layout_.locate(offset); head
        && parts_[head->row].is_open()
        && !parts_[head->row].on_disk().holds_offset(head->rest.offset)) {
        ++clamped_;
        return src.size();
    }
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

        const std::size_t fits = part.on_disk().clamp_write(hit->rest.offset, want);
        if (fits != want)
            ++clamped_;
        if (fits == 0) {
            done += want;
            continue;
        }

        std::memcpy(scratch_, src.data() + done, fits);
        if (plan_.rows[hit->row].byteswap) {
            normalize(std::span<std::uint8_t>(scratch_, fits), ByteOrder::Little);
        }

        auto existing = f->read_at(
            hit->rest.offset,
            std::as_writable_bytes(std::span<std::uint8_t>(compare_, fits))
        );
        if (existing && *existing == fits && std::memcmp(compare_, scratch_, fits) == 0) {
            ++unchanged_;
            done += want;
            continue;
        }
        auto n = f->write_at(
            hit->rest.offset,
            std::as_bytes(std::span<const std::uint8_t>(scratch_, fits))
        );
        if (!n || *n != fits)
            break;

        if (auto fl = f->flush(); !fl)
            break;
        done += want;
    }
    return done;
}

}
