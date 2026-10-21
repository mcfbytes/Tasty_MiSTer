// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/n64_save_store.h"

#include <algorithm>
#include <cstring>
#include <optional>
#include <utility>

#include "cores/n64_cpak.h"
#include "cores/n64_rom.h"
#include "infra/endian.h"

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

struct Hit {
    std::size_t index = 0;
    std::uint64_t offset = 0;
};

std::optional<Hit> resolve(
    const std::uint32_t* offset,
    const std::uint32_t* length,
    std::size_t rows,
    std::uint64_t absolute
) noexcept {
    for (std::size_t i = 0; i < rows; ++i) {
        const std::uint64_t end = static_cast<std::uint64_t>(offset[i]) + length[i];
        if (absolute < end)
            return Hit{i, absolute - offset[i]};
    }
    return std::nullopt;
}

}

SaveChannelStore::SaveChannelStore(const svc::Vfs& vfs) noexcept : vfs_(&vfs) {}

void SaveChannelStore::seed_(std::size_t i) noexcept {
    std::uint32_t off = 0;
    std::uint8_t n = 0;
    for (std::size_t r = 0; r < kMaxSaveRows; ++r) {
        if (rows_[i].declared[r] == 0)
            break;
        offset_[r] = off;
        length_[r] = rows_[i].declared[r];
        off += rows_[i].declared[r];
        ++n;
    }
    for (std::size_t r = 0; r < kMaxSaveRows; ++r)
        byteswap_[r] = rows_[i].byteswap[r];
    count_ = n;
    key_ = 0;
}

void SaveChannelStore::note_key(std::uint8_t key) noexcept {
    TASTY_SEAT_BODY(SaveChannelStore);
    if (count_ == 0 || key == key_)
        return;
    const SaveLayout live = expand_save_layout(key);
    if (!live.valid || live.count != count_) {
        ++layout_errors_;
        return;
    }
    for (std::uint8_t i = 0; i < count_; ++i) {
        offset_[i] = live.offset[i];
        length_[i] = live.length[i];
    }
    key_ = key;
}

Ex<std::size_t> SaveChannelStore::render_(const SaveOrder& b, std::size_t i) {
    const std::size_t sz = b.create;
    if (sz == 0 || sz > sizeof blank_) {
        return std::unexpected(Error{Errc::slot_range, ERR_SITE(), b.create});
    }
    std::memset(blank_, 0, sz);
    if (migrate_(b, i))
        return sz;
    if (b.cpak && sz == cpak::kImageBytes) {
        auto serial = cpak::random_serial();
        if (!serial)
            return std::unexpected(serial.error());
        cpak::render(*serial, std::span<std::uint8_t, cpak::kImageBytes>(blank_, sz));
        return sz;
    }
    std::memset(blank_, 0xFF, sz);
    return sz;
}

bool SaveChannelStore::migrate_(const SaveOrder& b, std::size_t i) {
    const std::string_view old = b.stock.view();
    if (old.empty() || !vfs_->file_exists(old))
        return false;
    auto f = vfs_->open(old, svc::OpenMode::ReadWhole);
    if (!f)
        return false;
    auto on_disk = (*f)->size();
    if (!on_disk)
        return false;
    const std::uint64_t end = std::min<std::uint64_t>(on_disk->v, kMaxPartitionBytes);
    if (b.offset >= end)
        return false;
    const std::size_t sz = b.create;
    const auto want = static_cast<std::size_t>(std::min<std::uint64_t>(sz, end - b.offset));
    auto got = (*f)->read_at(
        b.offset,
        std::as_writable_bytes(std::span<std::uint8_t>(blank_, want))
    );
    if (!got || *got == 0 || all_blank(blank_, sz))
        return false;
    if (b.byteswap[i])
        normalize(std::span<std::uint8_t>(blank_, *got), ByteOrder::Little);
    if (*got < sz)
        std::memset(blank_ + *got, 0xFF, sz - *got);
    ++migrated_;
    return true;
}

Ex<void> SaveChannelStore::create_(const SaveOrder& b, std::size_t bytes) {
    const std::string_view rel = b.path.view();
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

Ex<proto::FileSize> SaveChannelStore::open(std::size_t i, std::span<const std::byte> order) {
    TASTY_SEAT_BODY(SaveChannelStore);
    if (i >= kMaxSaveRows) {
        return std::unexpected(Error{Errc::slot_range, ERR_SITE(), static_cast<std::uint32_t>(i)});
    }

    files_[i].reset();
    on_disk_[i] = 0;

    if (order.size() != sizeof(SaveOrder)) {
        return std::unexpected(
            Error{Errc::mount_failed, ERR_SITE(), static_cast<std::uint32_t>(i)}
        );
    }
    rows_[i] = infra::load_native<SaveOrder>(order.first<sizeof(SaveOrder)>());
    seed_(i);
    const SaveOrder& b = rows_[i];
    if (b.path.view().empty()) {
        return std::unexpected(
            Error{Errc::mount_failed, ERR_SITE(), static_cast<std::uint32_t>(i)}
        );
    }
    if (b.create != 0 && !vfs_->file_exists(b.path.view())) {
        auto n = render_(b, i);
        if (!n)
            return std::unexpected(n.error());
        if (auto r = create_(b, *n); !r)
            return std::unexpected(r.error());
    }
    auto f = vfs_->open(b.path.view(), svc::OpenMode::ReadWrite);
    if (!f)
        return std::unexpected(f.error());
    auto size = (*f)->size();
    if (!size)
        return std::unexpected(size.error());
    files_[i] = std::move(*f);
    on_disk_[i] = static_cast<std::uint32_t>(size->v);
    return proto::FileSize{on_disk_[i]};
}

void SaveChannelStore::close(std::size_t i) noexcept {
    TASTY_SEAT_BODY(SaveChannelStore);
    if (i >= kMaxSaveRows)
        return;
    files_[i].reset();
    on_disk_[i] = 0;
}

Ex<std::size_t> SaveChannelStore::read_at(std::uint64_t offset, std::span<std::uint8_t> dst) {
    TASTY_SEAT_BODY(SaveChannelStore);
    if (count_ == 0) {
        ++stray_;
        return std::size_t{0};
    }
    std::size_t done = 0;
    while (done < dst.size()) {
        auto hit = resolve(offset_, length_, count_, offset + done);
        if (!hit)
            break;
        const auto room = static_cast<std::size_t>(length_[hit->index] - hit->offset);
        const std::size_t want = std::min(dst.size() - done, room);
        svc::IFile* const f = files_[hit->index].get();
        if (f == nullptr)
            break;
        auto got = f->read_at(hit->offset, std::as_writable_bytes(dst.subspan(done, want)));
        if (!got)
            break;
        if (byteswap_[hit->index]) {
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
    if (count_ == 0) {
        ++stray_;
        return std::size_t{0};
    }

    if (auto head = resolve(offset_, length_, count_, offset);
        head && files_[head->index] != nullptr && head->offset >= on_disk_[head->index]) {
        ++clamped_;
        return src.size();
    }
    std::size_t done = 0;
    while (done < src.size()) {
        auto hit = resolve(offset_, length_, count_, offset + done);
        if (!hit)
            break;
        const auto room = static_cast<std::size_t>(length_[hit->index] - hit->offset);
        const std::size_t want = std::min({src.size() - done, room, kMaxSliceBytes});
        svc::IFile* const f = files_[hit->index].get();
        if (f == nullptr)
            break;

        const std::uint64_t on_disk = on_disk_[hit->index];
        const std::size_t fits = hit->offset < on_disk
            ? static_cast<std::size_t>(std::min<std::uint64_t>(want, on_disk - hit->offset))
            : 0;
        if (fits != want)
            ++clamped_;
        if (fits == 0) {
            done += want;
            continue;
        }

        std::memcpy(scratch_, src.data() + done, fits);
        if (byteswap_[hit->index]) {
            normalize(std::span<std::uint8_t>(scratch_, fits), ByteOrder::Little);
        }

        auto existing = f->read_at(
            hit->offset,
            std::as_writable_bytes(std::span<std::uint8_t>(compare_, fits))
        );
        if (existing && *existing == fits && std::memcmp(compare_, scratch_, fits) == 0) {
            ++unchanged_;
            done += want;
            continue;
        }
        auto n = f->write_at(
            hit->offset,
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

Ex<std::size_t> SavePort::read_at(std::uint64_t offset, std::span<std::uint8_t> dst) {
    TASTY_SEAT_BODY(SavePort);
    return store_->read_at(offset, dst);
}

Ex<std::size_t> SavePort::write_at(std::uint64_t offset, std::span<const std::uint8_t> src) {
    TASTY_SEAT_BODY(SavePort);
    return store_->write_at(offset, src);
}

Ex<proto::FileSize> SavePort::open_extent(std::span<const std::byte> order) {
    TASTY_SEAT_BODY(SavePort);
    return store_->open(index_, order);
}

void SavePort::note_tag(std::uint8_t tag) noexcept {
    TASTY_SEAT_BODY(SavePort);
    store_->note_key(tag);
}

void SavePort::release() noexcept {
    TASTY_SEAT_BODY(SavePort);
    store_->close(index_);
}

}
