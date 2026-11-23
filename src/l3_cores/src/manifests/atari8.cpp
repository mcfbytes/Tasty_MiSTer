// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/manifests/atari8.h"

#include <memory>

#include "cores/atari8/atari8_core.h"
#include "cores/atari8/atari8_fill.h"
#include "cores/atari8/atari8_loader.h"
#include "cores/atari8/atari8_machine.h"
#include "cores/atari8/atari8_servant.h"
#include "cores/atari8/atari8_status.h"
#include "cores/atari8/atari8_status2.h"
#include "cores/atari8/atari8_wire.h"
#include "cores/atari8/pbi_drive_mode.h"
#include "cores/atari8/sio_dma.h"
#include "cores/atari8/sio_engine.h"
#include "cores/atari8/sio_wire.h"
#include "cores/servant_grant.h"
#include "proto/link/spi_fio_queue.h"
#include "proto/mailbox_act.h"
#include "proto/status_word.h"
#include "proto/user_io/poll_frame.h"
#include "svc/vfs.h"

namespace mister::cores::manifests {

using atari8::Atari8Machine;

std::unique_ptr<Core> make_atari800(const CoreProfile& p, const CoreGrant& g) {
    return std::make_unique<atari8::Atari8Core>(
        p,
        g.services,
        atari8::kAtari800Mailbox,
        Atari8Machine::Computer
    );
}

std::unique_ptr<Core> make_atari5200(const CoreProfile& p, const CoreGrant& g) {
    return std::make_unique<atari8::Atari8Core>(
        p,
        g.services,
        kAtari8Mailbox,
        Atari8Machine::Console
    );
}

std::unique_ptr<ILoader> make_atari800_loader(const LoaderContext& ctx) {
    return std::make_unique<atari8::Atari8Loader>(ctx, Atari8Machine::Computer);
}

std::unique_ptr<ILoader> make_atari5200_loader(const LoaderContext& ctx) {
    return std::make_unique<atari8::Atari8Loader>(ctx, Atari8Machine::Console);
}

std::unique_ptr<IMailboxServant> make_atari800_servant(const ServantGrant& g) {
    if (!g.clock)
        return nullptr;
    return std::make_unique<atari8::Atari8Servant>(
        atari8::Atari8Disks::Wiring{.vfs = g.vfs, .clock = *g.clock}
    );
}

std::unique_ptr<IMailboxServant> make_atari5200_servant(const ServantGrant& g) {
    (void)g;
    return std::make_unique<atari8::Atari8Servant>();
}

namespace wire = atari8::wire;
using hal::SpiWord;
using proto::MailboxRunCommand;

static_assert(kAtari800.kind == CoreKind::Atari800 && kAtari5200.kind == CoreKind::Atari5200);
static_assert(
    kAtari800.services.size() == 1
        && kAtari5200.services.size() == 1
        && kAtari800.services[0].period == reactor::kEveryRound
        && kAtari5200.services[0].period == reactor::kEveryRound,
    "user_io.cpp:3833-3834: atari800_poll and atari5200_poll run on every pass"
);

static_assert(
    wire::word(wire::Opcode::GetRegister) == SpiWord{0x08}
        && wire::word(wire::Opcode::SetRegister) == SpiWord{0x09},
    "atari800.cpp:240-256: A800_SET_REGISTER 0x09 and A800_GET_REGISTER 0x08"
);
static_assert(
    wire::pause(wire::Pause::Hold)
            == MailboxRunCommand{.opcode = SpiWord{0x09}, .word = SpiWord{0x0401}}
        && wire::pause(wire::Pause::Run)
            == MailboxRunCommand{.opcode = SpiWord{0x09}, .word = SpiWord{0x0400}}
        && wire::pause(wire::Pause::Halted)
            == MailboxRunCommand{.opcode = SpiWord{0x09}, .word = SpiWord{0x0404}},
    "atari800.cpp:244: spi_w((reg << 8) | val), REG_PAUSE 4; :358 writes the halt bit "
    "as it stands, so a halted machine's PAUSE value is 4"
);
static_assert(
    wire::set(wire::WriteRegister::Cart1Select, wire::Level::Low).word == SpiWord{0x0100}
        && wire::set(wire::WriteRegister::Cart2Select, wire::Level::Low).word == SpiWord{0x0200}
        && wire::set(wire::WriteRegister::Reset, wire::Level::High).word == SpiWord{0x0301}
        && wire::set(wire::WriteRegister::Freezer, wire::Level::High).word == SpiWord{0x0501}
        && wire::set(wire::WriteRegister::ResetRnmi, wire::Level::High).word == SpiWord{0x0601}
        && wire::set(wire::WriteRegister::XexLoader, wire::Level::Low).word == SpiWord{0x0900}
        && wire::set(wire::WriteRegister::TapeReset, wire::Level::High).word == SpiWord{0x0C01},
    "atari800.cpp's REG_ write numbers: CART1 1, CART2 2, RESET 3, FREEZER 5, RESET_RNMI 6, "
    "XEX_LOADER 9, TAPE_RESET 0x0C"
);
static_assert(
    wire::get(wire::ReadRegister::Status1)
            == MailboxRunCommand{.opcode = SpiWord{0x08}, .word = SpiWord{0x0100}}
        && wire::get(wire::ReadRegister::Status3)
            == MailboxRunCommand{.opcode = SpiWord{0x08}, .word = SpiWord{0x0300}},
    "atari800.cpp:253: spi_w(reg << 8) answers the register; STATUS1 1, STATUS3 3"
);
static_assert(
    atari8::Atari8Status::kSoftBoot == SpiWord{0x0001}
        && atari8::Atari8Status::kColdBoot == SpiWord{0x0002}
        && atari8::Atari8Status::kHalt == SpiWord{0x0004}
        && atari8::Atari8Status::kMode800 == SpiWord{0x0008},
    "atari800.cpp's STATUS1_MASK_ SOFTBOOT, COLDBOOT, HALT, MODE800"
);

static_assert(
    atari8::kStatusFrame.row().opcode == SpiWord{0x08}
        && atari8::kStatusFrame.row().exchanged == SpiWord{0x0100}
        && atari8::kStatusFrame.kept() == proto::ReplyCount{1}
        && atari8::kStatusFrame.max_words() == proto::FioWordCount{2}
        && atari8::kStatusFrame.held_words() == proto::FioWordCount{2},
    "atari800.cpp:356, atari5200.cpp:264: get_a8bit_reg(REG_ATARI_STATUS1) per pass"
);
static_assert(
    kAtari8Mailbox.polls().size() == 1
    && kAtari8Mailbox.guards().empty()
    && kAtari8Mailbox.polls()[0].relent_at_bind
);

static_assert(
    kAtari800Mailbox.guards()[1].row().exchanged == SpiWord{0x0200}
        && kAtari800Mailbox.guards()[1].row().guard.mask == proto::ReplyMask{0x8000}
        && kAtari800Mailbox.guards()[1].row().guard.condition == proto::user_io::Condition::Zero
        && atari8::kTapeGuard == proto::MailboxRunGuard{1},
    "atari800.cpp:402-405: atari800_tape_enqueue waits while STATUS2_MASK_TAPE_FULL "
    "(0x8000) reads set"
);
static_assert(
    wire::enqueue(
        hal::SpiHalves::of(0x0003'4567)
    ) == MailboxRunCommand{.opcode = SpiWord{0x0A}, .word = SpiWord{0x4567}, .tail = SpiWord{0x0003}},
    "atari800.cpp:420-424: spi8(A800_TAPE_ENQUEUE), spi_w(data), spi_w(data >> 16)"
);

static_assert(
    wire::read(wire::SioPort::TxStatus).opcode == SpiWord{0x03}
        && wire::read(wire::SioPort::Rx).opcode == SpiWord{0x04}
        && wire::read(wire::SioPort::RxStatus).opcode == SpiWord{0x05}
        && wire::read(wire::SioPort::Divisor).opcode == SpiWord{0x06}
        && wire::read(wire::SioPort::Error).opcode == SpiWord{0x07}
        && wire::read(wire::SioPort::Rx).word == SpiWord{},
    "atari800.cpp:258-265 get_a800_reg2: spi8(reg), spi_w(0); A800_SIO_TX_STAT 3, "
    "RX 4, RX_STAT 5, GETDIV 6, ERROR 7"
);
static_assert(
    wire::transmit(wire::kAck) == MailboxRunCommand{SpiWord{0x09}, SpiWord{0x0A41}}
        && wire::divisor(atari8::SioDivisor{0x5D})
            == MailboxRunCommand{SpiWord{0x09}, SpiWord{0x0B5D}}
        && wire::set(wire::WriteRegister::DriveLed, wire::Level::High).word == SpiWord{0x0801}
        && wire::set(wire::WriteRegister::OptionForce, wire::Level::High).word == SpiWord{0x0701},
    "atari800.cpp:462,488: REG_SIO_SETDIV 0x0B, REG_SIO_TX 0x0A; :2046 REG_DRIVE_LED 8; "
    ":2447 REG_OPTION_FORCE 7; :2781 uart_init(speeds[0] + 6) writes (0x2E << 1) + 1"
);
static_assert(
    wire::kTxRoomGuard.row().opcode == SpiWord{0x03}
        && wire::kTxRoomGuard.row().guard.mask == proto::ReplyMask{0x01FF}
        && wire::kRxFrame.row().opcode == SpiWord{0x05}
        && wire::kRxFrame.row().guard.mask == proto::ReplyMask{0x02FF},
    "atari800.cpp:467 uart_full: TX_STAT bit 9; :493 uart_available: RX_STAT bit 8 "
    "clear; hps_ext.v's FIFO count wraps to 0 at full, so a nonzero 9-bit field is "
    "not-full and a nonzero full-or-count field is not-empty"
);
static_assert(
    kAtari800Mailbox.polls().size() == 2
        && kAtari800Mailbox.guards().size() == 2
        && kAtari800Mailbox.polls()[0].relent_at_bind
        && !kAtari800Mailbox.polls()[1].relent_at_bind,
    "the computer lends its status at a bind; the receive watch lends while it holds"
);
static_assert(
    atari8::kComputerStatusMask == proto::ReplyMask{0x87C7},
    "atari800.cpp:1016 ATX1050 0x8000, :1489 RDONLY 0x40, :2555 MODEPBI 0x80, "
    "Atari800.sv status1[10:8] the HSIO index"
);
static_assert(
    atari8::SioEngine::kPreAck.count() == 100
        && atari8::SioEngine::kPreAckTimed.count() == 3220
        && atari8::SioEngine::kPreComplete.count() == 600
        && atari8::SioEngine::kPreData.count() == 150
        && atari8::SioEngine::kPreWriteAck.count() == 850,
    "atari800.cpp:881-883 DELAY_T2_MIN 100, DELAY_T5_MIN 600, DELAY_T3_PERIPH 150; "
    ":1982 the ATX read's 3220; :1779 the write's 850"
);

static_assert(
    (proto::kMailboxRunCommandWords + wire::kTxRoomGuard.max_words())
            .times(proto::user_io::kMaxPollFrameReplies)
        <= proto::kMailboxDirectWords,
    "every staged TX run fits one round's direct budget behind its guard"
);
static_assert(
    proto::link::SpiFioQueue::kUploadFramingWords
            + proto::FioWordCount::carrying(
                proto::FileSize::of(atari8::kMostUpload),
                hal::Width::Byte
            )
        <= proto::kMailboxDirectWords,
    "a PBI or XEX read of kMostUpload bytes fits one round's direct budget"
);

static_assert(
    atari8::Atari8Status2{SpiWord{0x0024}}.mode_of(atari8::SioUnit{0}) == atari8::PbiDriveMode::Off
        && atari8::Atari8Status2{SpiWord{0x0024}}.mode_of(atari8::SioUnit{1})
            == atari8::PbiDriveMode::Pbi
        && atari8::Atari8Status2{SpiWord{0x0024}}.mode_of(atari8::SioUnit{2})
            == atari8::PbiDriveMode::Sio
        && !atari8::Atari8Status2{}.mode_of(atari8::SioUnit{4}),
    "atari800.cpp:2568-2571: drive i's mode is (STATUS2 >> 2i) & 3, for item"
);
static_assert(
    atari8::Atari8Status2{SpiWord{0x0300}}.boot_drive() == atari8::Atari8Status2::BootDrive{3}
        && atari8::Atari8Status2::kSplash == SpiWord{0x0800},
    "atari800.cpp:2574 boot_drv = (STATUS2 >> 8) & 7; atari8bit_defs.h:11 STATUS2_MASK_SPLASH"
);
static_assert(
    atari8::answer_byte(atari8::PbiDriveMode::Off) == std::byte{0xFF}
        && atari8::answer_byte(atari8::PbiDriveMode::Pbi) == std::byte{0x00}
        && atari8::answer_byte(atari8::PbiDriveMode::Sio) == std::byte{0x01},
    "atari800.cpp:2503-2506: an Off request returns 0xFF, the rest mode - 1"
);

static_assert(
    wire::kDmaIndex == proto::WideIoIndex{99},
    "atari800.cpp:293, atari5200.cpp:34: user_io_set_index(99)"
);
static_assert(
    atari8::Atari8Fill::kComputerRam.at().aux() == proto::AuxValue{0x0200'0000}
        && atari8::Atari8Fill::kConsoleRam.at().aux() == proto::AuxValue{0x0001'0000}
        && atari8::Atari8Fill::kConsoleCart.at().aux() == proto::AuxValue{0x0200'4000},
    "atari800.cpp:294 SDRAM_BASE; atari5200.cpp:35 ATARI_BASE; :297 SDRAM_BASE + 0x4000"
);

static_assert(
    kAtari800.host_bits.size() == 1
        && kAtari800.host_bits[0].fact == HostFact::StartRomInhibit
        && kAtari800.host_bits[0].bits == proto::BitField{proto::StatusBit{41}, 1},
    "captured CONF_STR: P2O[41],Use bootX.rom,Enabled,Disabled; Atari800.sv puts "
    "~status[41] in the status register's bit 4, which atari800.cpp:2750 reads"
);
static_assert(
    host_value(
        proto::StatusWord{}.with(proto::StatusBit{41}, true),
        kAtari800.host_bits,
        HostFact::StartRomInhibit
    ) == proto::FieldValue{1}
        && host_value(proto::StatusWord{}, kAtari800.host_bits, HostFact::StartRomInhibit)
            == proto::FieldValue{},
    "bit 41 set is Disabled: boot0..2.rom stay off the wire"
);
static_assert(kAtari5200.host_bits.empty(), "atari5200.cpp:288 sends boot0.rom unconditionally");

static_assert(
    kAtari800StartAssets.size() <= kMaxStartAssets
        && kAtari800StartAssets[0].dest == WideIoIndex{0x00}
        && kAtari800StartAssets[1].dest == WideIoIndex{0x40}
        && kAtari800StartAssets[2].dest == WideIoIndex{0x80}
        && kAtari800StartAssets[3].dest == WideIoIndex{0xC0}
        && kAtari800StartAssets[4].dest == WideIoIndex{0x07},
    "atari800.cpp:2752-2761: boot0..2.rom at i << 6, boot3.rom at 3 << 6, "
    "sid_data.bin at 7"
);
static_assert(
    kAtari800StartAssets[0].unless
        && kAtari800StartAssets[1].unless
        && kAtari800StartAssets[2].unless
        && !kAtari800StartAssets[3].unless
        && !kAtari800StartAssets[4].unless,
    "atari800.cpp:2750-2761: only boot0..2.rom sit under the option"
);
static_assert(
    kAtari5200StartAssets.size() == 1 && kAtari5200StartAssets[0].dest == WideIoIndex{},
    "atari5200.cpp:288-289: boot0.rom at index 0"
);

}
