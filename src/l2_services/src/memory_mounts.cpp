// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/memory_mounts.h"

#include "infra/prelude.h"
#include "observed_file.h"
#include "infra/span_cut.h"
#include "infra/tally.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <optional>
#include <utility>

namespace mister::svc {

namespace {

class MemoryFile final : public IFile {
public:
    MemoryFile(std::span<const std::byte> blob, std::shared_ptr<std::atomic<std::uint32_t>> hints)
        : blob_(blob),
          hints_(std::move(hints)) {}

    [[nodiscard]] Ex<infra::ByteCount> read_at(
        proto::FileOffset at,
        std::span<std::byte> dst
    ) override {
        if (!FileSize::of(blob_).holds(at))
            return infra::ByteCount{};
        const std::span<const std::byte> tail = infra::affine::from(blob_, at);
        const std::size_t n = std::min(tail.size(), dst.size());
        std::memcpy(dst.data(), tail.data(), n);
        return infra::ByteCount{n};
    }

    [[nodiscard]] Ex<infra::ByteCount> write_at(
        proto::FileOffset,
        std::span<const std::byte>
    ) override {
        return std::unexpected(Error{Errc::io, ERR_SITE(), static_cast<std::uint32_t>(EROFS)});
    }

    [[nodiscard]] Ex<FileSize> size() const override { return FileSize{blob_.size()}; }

    [[nodiscard]] Ex<void> flush() override { return {}; }

    [[nodiscard]] Ex<void> advise(Access a, proto::FileOffset, FileSize) override {
        if (a == Access::Sequential)
            hints_->fetch_add(1, std::memory_order_relaxed);
        return {};
    }

private:
    std::span<const std::byte> blob_;
    std::shared_ptr<std::atomic<std::uint32_t>> hints_;
};

}

infra::OptRef<const MemoryMounts::Refusal> MemoryMounts::find_refusal_(
    std::string_view key,
    IoVerb verb
) const noexcept {
    for (const Refusal& r : refusals_)
        if (r.verb == verb && r.path == key)
            return infra::OptRef<const Refusal>{r};
    return std::nullopt;
}

std::optional<Error> MemoryMounts::refusal(std::string_view key, IoVerb verb) const noexcept {
    const auto r = find_refusal_(key, verb);
    if (!r)
        return std::nullopt;
    return r->err.refusal(Errc::os, ERR_SITE());
}

std::unique_ptr<IFile> MemoryMounts::observed(
    std::string_view key,
    std::unique_ptr<IFile> file
) const {
    const std::optional<Error> reads = refusal(key, IoVerb::Read);
    const auto writes = find_refusal_(key, IoVerb::Write);
    if (!meter_ && !reads && !writes)
        return file;
    std::optional<detail::ObservedFile::WriteRefusal> w{};
    if (writes)
        w = detail::ObservedFile::WriteRefusal{
            writes->err.refusal(Errc::os, ERR_SITE()),
            writes->grace
        };
    return std::make_unique<detail::ObservedFile>(std::move(file), meter_, reads, w);
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

infra::Tally MemoryMounts::sequential_hints(std::string_view key) const noexcept {
    const Blob* m = find_(key);
    return m == nullptr ? infra::Tally{} : infra::Tally{m->hints->load(std::memory_order_relaxed)};
}

}
