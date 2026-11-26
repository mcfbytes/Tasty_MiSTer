// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <tuple>
#include <type_traits>
#include <variant>

#include "cores/ide/ide_channel_row.h"
#include "cores/ide/ide_task_file.h"
#include "cores/ide/ide_transport.h"
#include "cores/ide/ide_types.h"
#include "hal/types.h"
#include "infra/byte_count.h"
#include "infra/byte_offset.h"
#include "infra/message_sum.h"
#include "infra/seat.h"
#include "proto/disk_act.h"
#include "proto/disk_address.h"
#include "proto/types.h"

namespace mister::cores::ide {

class IdeActWire final : public IIdeTransport {
    TASTY_SEAT_RESIDENT(Io);

public:
    static constexpr std::size_t kPresenceVerbsPerDrive = 2;
    static constexpr std::size_t kDrivesPerChannel = 2;
    static constexpr std::size_t kResetAnswerTaskFiles = 2;
    static constexpr std::size_t kMaxVerbsPerAnswer = kDrivesPerChannel * kPresenceVerbsPerDrive
        + kResetAnswerTaskFiles;
    static_assert(kMaxVerbsPerAnswer == 6, "the engine's worst answer is six verbs");

    static constexpr infra::ByteCount kTaskFileBytes{std::tuple_size_v<IdeTaskFile::Image>};

    explicit IdeActWire(const IdeChannelRow& row) noexcept : row_(row) {}

    void begin(proto::DiskUnit unit) noexcept;

    void clear() noexcept;

    [[nodiscard]] bool empty() const noexcept { return verb_ >= asked_; }

    [[nodiscard]] proto::DiskAct next(std::span<std::byte, proto::kDiskActBytes> bytes) noexcept;

    [[nodiscard]] bool receive_done() const noexcept { return receive_done_; }

    void received_taken() noexcept { receive_done_ = false; }

    void write_task_file(const IdeTaskFile& regs) noexcept override;
    void send_data(std::span<const std::byte> data) noexcept override;
    void receive_data(std::span<std::byte> into) noexcept override;
    void write_reg(IdeRegister reg, hal::LinkWord value) noexcept override;

private:
    struct Send {
        std::span<const std::byte> bytes;
    };

    struct Receive {
        std::span<std::byte> bytes;
    };

    struct TaskFile {
        IdeTaskFile regs;
    };

    struct WriteReg {
        IdeRegister reg;
        hal::LinkWord word;
    };

    enum class VerbKind : std::uint8_t { Send, Receive, TaskFile, WriteReg, kCount };
    using Verb = std::variant<Send, Receive, TaskFile, WriteReg>;
    static_assert(
        std::variant_size_v<Verb> == infra::ordinal(VerbKind::kCount),
        "IdeActWire: one VerbKind name per arm"
    );
    template <VerbKind K, class T>
    static constexpr bool
        kArmIs = std::is_same_v<std::variant_alternative_t<infra::ordinal(K), Verb>, T>;
    static_assert(
        kArmIs<VerbKind::Send, Send>
        && kArmIs<VerbKind::Receive, Receive>
        && kArmIs<VerbKind::TaskFile, TaskFile>
        && kArmIs<VerbKind::WriteReg, WriteReg>
    );

    void ask_(const Verb& verb) noexcept;
    [[nodiscard]] proto::DiskAddress address_(IdeRegister reg) const noexcept;
    [[nodiscard]] static infra::ByteCount piece_of_(
        infra::ByteOffset at,
        infra::ByteCount whole
    ) noexcept;

    const IdeChannelRow& row_;
    proto::DiskUnit unit_{};
    std::array<Verb, kMaxVerbsPerAnswer> verbs_{};
    std::size_t asked_ = 0;
    std::size_t verb_ = 0;
    infra::ByteOffset at_{};
    std::span<std::byte> in_piece_{};
    bool receive_done_ = false;
    bool first_ = true;
};

}
