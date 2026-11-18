// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <utility>

#include "infra/error.h"
#include "infra/opt_ref.h"
#include "svc/file.h"
#include "svc/io_meter.h"
#include "svc/types.h"

namespace mister::svc::detail {

class ObservedFile final : public IFile {
public:
    struct WriteRefusal {
        Error err;
        FileSize grace{};
    };

    ObservedFile(
        std::unique_ptr<IFile> inner,
        infra::OptRef<IIoMeter> meter,
        std::optional<Error> read_refusal,
        std::optional<WriteRefusal> write_refusal
    )
        : inner_(std::move(inner)),
          meter_(meter),
          read_refusal_(read_refusal),
          write_refusal_(write_refusal) {}

    [[nodiscard]] Ex<std::size_t> read_at(proto::FileOffset at, std::span<std::byte> dst) override {
        if (read_refusal_)
            return std::unexpected(*read_refusal_);
        auto n = inner_->read_at(at, dst);
        if (n && meter_)
            meter_->read(FileSize{*n});
        return n;
    }

    [[nodiscard]] Ex<std::size_t> write_at(
        proto::FileOffset at,
        std::span<const std::byte> src
    ) override {
        if (write_refusal_ && taken_ >= write_refusal_->grace)
            return std::unexpected(write_refusal_->err);
        auto n = inner_->write_at(at, src);
        if (!n)
            return n;
        taken_ += FileSize{*n};
        if (meter_)
            meter_->written(FileSize{*n});
        return n;
    }

    [[nodiscard]] Ex<FileSize> size() const override { return inner_->size(); }

    [[nodiscard]] Ex<void> flush() override {
        if (meter_)
            meter_->flushed();
        return inner_->flush();
    }

    [[nodiscard]] Ex<void> advise(Access a, proto::FileOffset at, FileSize len) override {
        return inner_->advise(a, at, len);
    }

private:
    std::unique_ptr<IFile> inner_;
    infra::OptRef<IIoMeter> meter_;
    std::optional<Error> read_refusal_;
    std::optional<WriteRefusal> write_refusal_;
    FileSize taken_{};
};

}
