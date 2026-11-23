// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

#include "infra/byte_count.h"
#include "infra/error.h"
#include "infra/indexed_array.h"
#include "infra/opt_ref.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "proto/track_shape.h"
#include "proto/types.h"
#include "svc/disk_image.h"
#include "svc/gcr.h"
#include "svc/track_image.h"

namespace mister::svc {

class GcrTrackImage final : public IDiskImage, public ITrackImage {
    TASTY_SEAT_RESIDENT(Io);

public:
    [[nodiscard]] static Ex<std::unique_ptr<GcrTrackImage>> open(
        std::unique_ptr<IDiskImage> inner,
        GcrLayout layout
    );

    GcrTrackImage(const GcrTrackImage&) = delete;
    GcrTrackImage& operator=(const GcrTrackImage&) = delete;

    [[nodiscard]] FileSize size() const noexcept override;
    [[nodiscard]] bool writable() const noexcept override;
    [[nodiscard]] Ex<infra::ByteCount> read_at(
        proto::FileOffset at,
        std::span<std::byte> dst
    ) override;
    [[nodiscard]] Ex<infra::ByteCount> write_at(
        proto::FileOffset at,
        std::span<const std::byte> src
    ) override;
    [[nodiscard]] infra::OptRef<ITrackImage> track_image() noexcept override;

    [[nodiscard]] proto::TrackShape track_shape() const noexcept override;
    [[nodiscard]] Ex<infra::ByteCount> read_track(
        const proto::TrackShape::Ask& ask,
        std::span<std::byte> dst
    ) override;
    [[nodiscard]] Ex<void> write_track(
        const proto::TrackShape::Ask& ask,
        std::span<const std::byte> src
    ) override;

    [[nodiscard]] GcrCaps caps() const noexcept;

private:
    struct Buffers {
        std::array<std::byte, gcr::kWorkBufferBytes> work{};
        std::array<std::byte, gcr::kSectorBufferBytes> sectors{};
    };

    class File {
        TASTY_SEAT_RESIDENT(Io);

    public:
        explicit File(std::unique_ptr<IDiskImage> inner) noexcept;

        [[nodiscard]] IDiskImage& image() noexcept { return *inner_; }

        [[nodiscard]] const IDiskImage& image() const noexcept { return *inner_; }

        [[nodiscard]] gcr::ImagePos end() const noexcept;
        [[nodiscard]] proto::FileOffset end_offset() const noexcept;

        [[nodiscard]] Ex<void> read(proto::FileOffset at, std::span<std::byte> dst);

        [[nodiscard]] Ex<proto::FileOffset> write(
            proto::FileOffset at,
            std::span<const std::byte> src
        );
        [[nodiscard]] Ex<proto::FileOffset> append(std::span<const std::byte> src);

    private:
        std::unique_ptr<IDiskImage> inner_;
        proto::FileOffset end_;
    };

    class ILayout {
    public:
        virtual ~ILayout() = default;
        [[nodiscard]] virtual GcrCaps caps() const noexcept = 0;
        [[nodiscard]] virtual Ex<gcr::TrackLength> read(
            gcr::TrackWord word,
            infra::ByteCount room,
            File& file,
            Buffers& buf
        ) = 0;
        [[nodiscard]] virtual Ex<void> write(
            gcr::TrackWord word,
            gcr::TrackLength length,
            File& file,
            Buffers& buf
        ) = 0;

    protected:
        ILayout() = default;
        ILayout(const ILayout&) = default;
        ILayout& operator=(const ILayout&) = default;
    };

    class Sectors final : public ILayout {
        TASTY_SEAT_RESIDENT(Io);

    public:
        Sectors(gcr::SectorMap map, gcr::DiskId id) noexcept : map_(map), id_(id) {}

        [[nodiscard]] GcrCaps caps() const noexcept override;
        [[nodiscard]] Ex<gcr::TrackLength> read(
            gcr::TrackWord word,
            infra::ByteCount room,
            File& file,
            Buffers& buf
        ) override;
        [[nodiscard]] Ex<void> write(
            gcr::TrackWord word,
            gcr::TrackLength length,
            File& file,
            Buffers& buf
        ) override;

    private:
        void decode_(gcr::TrackLength length, proto::BlockCount on_track, Buffers& buf) noexcept;

        gcr::SectorMap map_;
        gcr::DiskId id_;
    };

    class Tracks final : public ILayout {
        TASTY_SEAT_RESIDENT(Io);

    public:
        [[nodiscard]] static Ex<std::unique_ptr<Tracks>> open(IDiskImage& image);
        [[nodiscard]] GcrCaps caps() const noexcept override;
        [[nodiscard]] Ex<gcr::TrackLength> read(
            gcr::TrackWord word,
            infra::ByteCount room,
            File& file,
            Buffers& buf
        ) override;
        [[nodiscard]] Ex<void> write(
            gcr::TrackWord word,
            gcr::TrackLength length,
            File& file,
            Buffers& buf
        ) override;

    private:
        struct Slot {
            gcr::ImagePos at{};
            gcr::SpeedZone speed{};
        };

        explicit Tracks(gcr::TrackTotal slots) noexcept : slots_(slots) {}

        [[nodiscard]] static proto::FileOffset position_entry_(gcr::HalfTrack track) noexcept;
        [[nodiscard]] proto::FileOffset speed_entry_(gcr::HalfTrack track) const noexcept;

        [[nodiscard]] std::optional<gcr::ImagePos> next_entry_past_(
            gcr::ImagePos from
        ) const noexcept;

        gcr::TrackTotal slots_;
        infra::IndexedArray<Slot, gcr::kMostSlots, gcr::HalfTrack> table_{};
    };

    GcrTrackImage(File file, std::unique_ptr<ILayout> layout) noexcept;

    File file_;
    std::unique_ptr<ILayout> layout_;
    Buffers buf_{};
};

}
