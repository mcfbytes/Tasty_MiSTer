// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/memory_mounts.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <utility>

namespace mister::svc {

namespace {

class MemoryFile final : public IFile {
public:
    MemoryFile(std::span<const std::byte> blob, std::shared_ptr<std::atomic<std::uint32_t>> hints)
        : blob_(blob),
          hints_(std::move(hints)) {}

    [[nodiscard]] Ex<std::size_t> read_at(std::uint64_t off, std::span<std::byte> dst) override {
        if (off >= blob_.size())
            return std::size_t{0};
        const std::size_t start = static_cast<std::size_t>(off);
        const std::size_t n = std::min(blob_.size() - start, dst.size());
        std::memcpy(dst.data(), blob_.data() + start, n);
        return n;
    }

    [[nodiscard]] Ex<std::size_t> write_at(std::uint64_t, std::span<const std::byte>) override {
        return std::unexpected(Error{Errc::io, ERR_SITE(), static_cast<std::uint32_t>(EROFS)});
    }

    [[nodiscard]] Ex<FileSize> size() const override { return FileSize{blob_.size()}; }

    [[nodiscard]] Ex<void> flush() override { return {}; }

    FileKind kind() const noexcept override { return FileKind::Memory; }

    [[nodiscard]] Ex<void> advise(Access a, std::uint64_t, std::uint64_t) override {
        if (a == Access::Sequential)
            hints_->fetch_add(1, std::memory_order_relaxed);
        return {};
    }

private:
    std::span<const std::byte> blob_;
    std::shared_ptr<std::atomic<std::uint32_t>> hints_;
};

}

void MemoryMounts::mount(std::string key, std::span<const std::byte> blob, Crc32 crc) {
    for (Blob& m : blobs_) {
        if (m.path == key) {
            m.blob = blob;
            m.crc = crc;
            return;
        }
    }
    blobs_.push_back(
        Blob{std::move(key), blob, crc, std::make_shared<std::atomic<std::uint32_t>>(0u)}
    );
}

const MemoryMounts::Blob* MemoryMounts::find_(std::string_view key) const noexcept {
    for (const Blob& m : blobs_) {
        if (m.path == key)
            return &m;
    }
    return nullptr;
}

bool MemoryMounts::mounted(std::string_view key) const noexcept {
    return find_(key) != nullptr;
}

std::unique_ptr<IFile> MemoryMounts::open(std::string_view key) const {
    const Blob* m = find_(key);
    if (m == nullptr)
        return nullptr;
    return std::unique_ptr<IFile>(new MemoryFile(m->blob, m->hints));
}

std::unique_ptr<IFile> MemoryMounts::open_by_crc_under(std::string_view prefix, Crc32 crc) const {
    for (const Blob& m : blobs_) {
        if (m.crc == crc
            && m.path.size() > prefix.size()
            && std::string_view(m.path).substr(0, prefix.size()) == prefix) {
            return std::unique_ptr<IFile>(new MemoryFile(m.blob, m.hints));
        }
    }
    return nullptr;
}

std::uint32_t MemoryMounts::sequential_hints(std::string_view key) const noexcept {
    const Blob* m = find_(key);
    return m == nullptr ? 0u : m->hints->load(std::memory_order_relaxed);
}

}
