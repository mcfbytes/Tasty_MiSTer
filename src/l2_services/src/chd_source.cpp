// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/chd_source.h"

#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <span>
#include <string_view>

#include "svc/chd_meta_row.h"
#include "svc/vfs.h"

#include <libchdr/chd.h>

namespace mister::svc {

namespace {

struct CoreFileBinding;

std::uint64_t cf_fsize(core_file* f);
std::size_t cf_fread(void* dst, std::size_t elem, std::size_t count, core_file* f);
int cf_fclose(core_file* f);
int cf_fseek(core_file* f, std::int64_t off, int whence);

struct CoreFileBinding {
    CoreFileBinding(IFile& source, std::uint64_t byte_size) noexcept
        : file(&source),
          size(byte_size) {
        cf.argp = this;
        cf.fsize = &cf_fsize;
        cf.fread = &cf_fread;
        cf.fclose = &cf_fclose;
        cf.fseek = &cf_fseek;
    }

    core_file cf{};
    IFile* file;
    std::uint64_t pos = 0;
    std::uint64_t size;
};

std::uint64_t cf_fsize(core_file* f) {
    auto* b = static_cast<CoreFileBinding*>(f->argp);
    return b->size;
}

std::size_t cf_fread(void* dst, std::size_t elem, std::size_t count, core_file* f) {
    auto* b = static_cast<CoreFileBinding*>(f->argp);
    const std::uint64_t bytes = static_cast<std::uint64_t>(elem) * count;
    if (elem == 0 || bytes == 0)
        return 0;
    auto rd = b->file->read_at(
        b->pos,
        std::span<std::byte>(static_cast<std::byte*>(dst), static_cast<std::size_t>(bytes))
    );
    if (!rd)
        return 0;
    b->pos += *rd;
    return *rd / elem;
}

int cf_fclose(core_file*) {
    return 0;
}

int cf_fseek(core_file* f, std::int64_t off, int whence) {
    auto* b = static_cast<CoreFileBinding*>(f->argp);
    std::int64_t base = 0;
    if (whence == SEEK_CUR)
        base = static_cast<std::int64_t>(b->pos);
    else if (whence == SEEK_END)
        base = static_cast<std::int64_t>(b->size);
    const std::int64_t next = base + off;
    if (next < 0)
        return -1;
    b->pos = static_cast<std::uint64_t>(next);
    return 0;
}

struct OpenedHandle {
    chd_file* chd;
    const chd_header* header;
    std::uint32_t err;
};

[[nodiscard]] OpenedHandle open_handle(CoreFileBinding& b) noexcept {
    chd_file* chd = nullptr;
    const chd_error err = chd_open_core_file(&b.cf, CHD_OPEN_READ, nullptr, &chd);
    if (err != CHDERR_NONE) {
        return OpenedHandle{nullptr, nullptr, static_cast<std::uint32_t>(err)};
    }
    const chd_header* header = chd_get_header(chd);
    if (header == nullptr) {
        chd_close(chd);
        return OpenedHandle{nullptr, nullptr, 0};
    }
    return OpenedHandle{chd, header, 0};
}

class LibChdrSource final : public IChdSource {
    struct Key {};

public:
    [[nodiscard]] static Ex<std::unique_ptr<LibChdrSource>> open(
        std::unique_ptr<IFile> file,
        std::uint64_t size
    ) {
        auto src = std::make_unique<LibChdrSource>(Key{}, std::move(file), size);
        if (src->handle_.chd == nullptr) {
            return std::unexpected(Error{Errc::bad_format, ERR_SITE(), src->handle_.err});
        }
        return src;
    }

    LibChdrSource(Key, std::unique_ptr<IFile> file, std::uint64_t size) noexcept
        : file_(std::move(file)),
          binding_(*file_, size),
          handle_(open_handle(binding_)) {}

    ~LibChdrSource() override {
        if (handle_.chd != nullptr)
            chd_close(handle_.chd);
    }

    LibChdrSource(const LibChdrSource&) = delete;
    LibChdrSource& operator=(const LibChdrSource&) = delete;

    Geometry geometry() const noexcept override {
        return Geometry{
            handle_.header->hunkbytes,
            handle_.header->unitbytes,
            handle_.header->totalhunks
        };
    }

    std::optional<ChdMetaRow> track_metadata(std::uint32_t index) override {

        if (chd_get_metadata(
                handle_.chd,
                CDROM_TRACK_METADATA2_TAG,
                index,
                meta_,
                sizeof(meta_),
                nullptr,
                nullptr,
                nullptr
            )
            == CHDERR_NONE) {
            meta_[sizeof(meta_) - 1] = '\0';
            return ChdMetaRow{std::string_view(meta_), true};
        }
        if (chd_get_metadata(
                handle_.chd,
                CDROM_TRACK_METADATA_TAG,
                index,
                meta_,
                sizeof(meta_),
                nullptr,
                nullptr,
                nullptr
            )
            == CHDERR_NONE) {
            meta_[sizeof(meta_) - 1] = '\0';
            return ChdMetaRow{std::string_view(meta_), false};
        }
        return std::nullopt;
    }

    [[nodiscard]] Ex<void> read_hunk(std::uint32_t hunk, std::span<std::byte> dst) override {
        if (dst.size() < handle_.header->hunkbytes) {
            return std::unexpected(
                Error{Errc::short_read, ERR_SITE(), static_cast<std::uint32_t>(dst.size())}
            );
        }
        const chd_error err = chd_read(handle_.chd, hunk, dst.data());
        if (err != CHDERR_NONE) {
            return std::unexpected(Error{Errc::io, ERR_SITE(), static_cast<std::uint32_t>(err)});
        }
        return {};
    }

private:
    std::unique_ptr<IFile> file_;
    CoreFileBinding binding_;
    OpenedHandle handle_;
    char meta_[512]{};
};

}

Ex<std::unique_ptr<IChdSource>> open_chd(std::unique_ptr<IFile> file) {
    if (!file)
        return std::unexpected(Error{Errc::mount_failed, ERR_SITE(), 0});
    auto sz = file->size();
    if (!sz)
        return std::unexpected(sz.error());
    auto src = LibChdrSource::open(std::move(file), sz->v);
    if (!src)
        return std::unexpected(src.error());
    return std::unique_ptr<IChdSource>(std::move(*src));
}

}
