#include "nec765_floppy_controller.h"
#include "disk_data.h"
#include <stdexcept>
#include <format>
#include <print>
#include <cassert>
#include <cstring>
#include <utility>

namespace {

#define COMMANDS(X) \
   X(READ_TRACK,                 2) /* generates IRQ6 */ \
   X(SPECIFY,                    3) /* * set drive parameters */ \
   X(SENSE_DRIVE_STATUS,         4) \
   X(WRITE_DATA,                 5) /* * write to the disk */ \
   X(READ_DATA,                  6)  /* * read from the disk */ \
   X(RECALIBRATE,                7)  /* * seek to cylinder 0 */ \
   X(SENSE_INTERRUPT,            8)  /* * ack IRQ6, get status of last command */ \
   X(WRITE_DELETED_DATA,         9) \
   X(READ_ID,                    10)  /* generates IRQ6 */ \
   X(READ_DELETED_DATA,          12) \
   X(FORMAT_TRACK,               13) \
   X(DUMPREG,                    14) \
   X(SEEK,                       15) /* * seek both heads to cylinder X */ \
   X(VERSION,                    16) /* * used during initialization, once */ \
   X(SCAN_EQUAL,                 17) \
   X(PERPENDICULAR_MODE,         18) /* * used during initialization, once, maybe */ \
   X(CONFIGURE,                  19) /* * set controller parameters */ \
   X(LOCK,                       20) /* * protect controller params from a reset */ \
   X(VERIFY,                     22) \
   X(SCAN_LOW_OR_EQUAL,          25) \
   X(SCAN_HIGH_OR_EQUAL,         29)

enum : uint8_t {
#define DEF_COMMAND(name, value) CMD_##name = value,
    COMMANDS(DEF_COMMAND)
#undef DEF_COMMAND
};

constexpr uint8_t CMD_MASK = 31;

std::string CommandName(uint8_t command)
{
    switch (command & CMD_MASK) {
#define CASE_COMMAND(name, value) case value: return #name;
        COMMANDS(CASE_COMMAND);
#undef CASE_COMMAND
    default:
        return "Unknown command " + std::to_string(command);
    }
}

enum : uint8_t {
    NEC765_REG_SRA_R, // Status Register A
    NEC765_REG_SRB_R, // Status Register B
    NEC765_REG_DOR_RW, // Digital Output Register
    NEC765_REG_TDR_RW, // Tape Drive Register
    NEC765_REG_STR_R, // Status Register
    NEC765_REG_DRR_W = NEC765_REG_STR_R, // Data Rate Register
    NEC765_REG_DATA_RW, // Data Register with FIFO
    NEC765_REG_RESERVED, // Reserved
    NEC765_REG_DIR_R, // Digital Input Register
    NEC765_REG_CCR_W = NEC765_REG_DIR_R, // Configuration Control Register
};

enum : uint8_t {
    DOR_BIT_DSEL0,
    DOR_BIT_DSEL1,
    DOR_BIT_RESET_N,
    DOR_BIT_IRQ,
    DOR_BIT_MOT0,
    DOR_BIT_MOT1,
    DOR_BIT_MOT2,
    DOR_BIT_MOT3,
};

enum : uint8_t {
    DOR_MASK_DSEL = 3,
    DOR_MASK_RESET_N = 1 << DOR_BIT_RESET_N,
    DOR_MASK_IRQ = 1 << DOR_BIT_IRQ,
    DOR_MASK_MOT0 = 1 << DOR_BIT_MOT0,
    DOR_MASK_MOT1 = 1 << DOR_BIT_MOT1,
    DOR_MASK_MOT2 = 1 << DOR_BIT_MOT2,
    DOR_MASK_MOT3 = 1 << DOR_BIT_MOT3,
};

enum : uint8_t {
    STR_BIT_ACT0, // Drive 0 is seeking
    STR_BIT_ACT1,
    STR_BIT_ACT2,
    STR_BIT_ACT3,
    STR_BIT_CB, // Command Busy
    STR_BIT_NDMA, // Set in execution phase of PIO mode command
    STR_BIT_DIO, // IO port expectes IN
    STR_BIT_RQM, // Set if bytes can be exchanged with FIFO
};

enum : uint8_t {
    STR_MASK_ACT0 = 1 << STR_BIT_ACT0,
    STR_MASK_ACT1 = 1 << STR_BIT_ACT1,
    STR_MASK_ACT2 = 1 << STR_BIT_ACT2,
    STR_MASK_ACT3 = 1 << STR_BIT_ACT3,
    STR_MASK_CB = 1 << STR_BIT_CB,
    STR_MASK_NDMA = 1 << STR_BIT_NDMA,
    STR_MASK_DIO = 1 << STR_BIT_DIO,
    STR_MASK_RQM = 1 << STR_BIT_RQM,
};

enum : uint8_t {
    ST0_BIT_DS0,
    ST0_BIT_DS1,
    ST0_BIT_HEAD,
    ST0_BIT_UNUSED,
    ST0_BIT_EC, // Equipment check
    ST0_BIT_SE, // Seek END
    ST0_BIT_IC0, // Interrupt code
    ST0_BIT_IC1,
};

enum : uint8_t {
    ST0_MASK_DS = 3,
    ST0_MASK_HEAD = 1 << ST0_BIT_HEAD,
    ST0_MASK_EC = 1 << ST0_BIT_EC,
    ST0_MASK_SE = 1 << ST0_BIT_SE,
    ST0_MASK_IC = 3 << ST0_BIT_IC0,
};


} // unnamed namespace

class NEC765_FloppyController::impl : public IOHandler, public CycleObserver, public DMAHandler {
public:
    using OnInterrupt = std::function<void(void)>;

    explicit impl(SystemBus& bus, const OnInterrupt& onInt, const OnDmaStart& onDmaStart, bool reducedIORange);

    void reset();

    void runCycles(std::uint64_t cycles) override;
    std::uint64_t nextAction() override;

    std::uint8_t inU8(uint16_t port, uint16_t) override;
    void outU8(uint16_t port, uint16_t, std::uint8_t value) override;

    uint8_t dmaGetU8() override;
    void dmaPutU8(uint8_t) override;
    void dmaDone() override;

    void insertDisk(uint8_t drive, const std::vector<uint8_t>& data)
    {
        assert(drive < 4);
        diskData_[drive].insert(std::vector<uint8_t>(data));
    }

    void insertDisk(uint8_t drive, std::string_view filename)
    {
        assert(drive < 4);
        diskData_[drive].insert(filename);
    }

private:
    SystemBus& bus_;
    OnInterrupt onInt_;
    OnDmaStart onDmaStart_;
    uint64_t cycles_;

    using TransitionFunc = std::function<void(void)>;

    uint64_t nextTransition_;
    TransitionFunc transition_;

    void setTransition(uint64_t cycles, const TransitionFunc& func);
    void raiseIRQ();
    void getCommandArgs();
    void executeCommand();
    void setSt0(uint8_t info);

    enum class State {
        Initial,
        Reset,
        CommandPhase,
        CommandArgsPhase,
        ExecutionPhase,
        ResultPhase,
    } state_;
    uint8_t dor_;
    uint8_t command_;
    uint8_t argsCnt_;
    uint8_t resetCnt_;
    std::vector<uint8_t> commandArgs_;
    std::vector<uint8_t> result_;
    uint8_t st0_; // Status Register 0
    uint8_t curDrive_;
    struct DriveState {
        uint8_t cylinder;
        uint8_t head;
        uint8_t sector;
        uint16_t sectorOffset;
    } driveState_[4];
    DiskData diskData_[4] {};
};

NEC765_FloppyController::impl::impl(SystemBus& bus, const OnInterrupt& onInt, const OnDmaStart& onDmaStart, bool reducedIORange)
    : bus_(bus)
    , onInt_(onInt)
    , onDmaStart_(onDmaStart)
{
    bus.addIOHandler(0x3f0, reducedIORange ? 6 : 8, *this, true);
    bus.addCycleObserver(*this);
    reset();
}

void NEC765_FloppyController::impl::reset()
{
    cycles_ = 0;
    transition_ = TransitionFunc {};
    nextTransition_ = 0;
    state_ = State::Initial;
    dor_ = 0;
    command_ = 0;
    st0_ = 0;
    resetCnt_ = 0;
    curDrive_ = 0;
    std::memset(driveState_, 0, sizeof(driveState_));
    result_.clear();
}

void NEC765_FloppyController::impl::runCycles(std::uint64_t cycles)
{
    if (transition_) {
        cycles_ += cycles;
        if (cycles_ >= nextTransition_) {
            std::exchange(transition_, TransitionFunc {})();
            cycles_ = 0;
        }
    } else {
        cycles_ = 0;
    }
}

std::uint64_t NEC765_FloppyController::impl::nextAction()
{
    if (transition_) {
        assert(nextTransition_ >= cycles_);
        return nextTransition_ - cycles_;
    } else {
        return UINT64_MAX;
    }
}

void NEC765_FloppyController::impl::setTransition(uint64_t cycles, const TransitionFunc& func)
{
    nextTransition_ = cycles_ + cycles;
    transition_ = func;
    bus_.recalcNextAction();
}

void NEC765_FloppyController::impl::raiseIRQ()
{
    if (dor_ & DOR_MASK_IRQ) {
        std::println("Floppy: IRQ");
        onInt_();
    } else {
        std::println("Floppy: IRQ suppressed");
    }
}

std::uint8_t NEC765_FloppyController::impl::inU8(uint16_t port, uint16_t offset)
{
    switch (offset) {
    case NEC765_REG_SRB_R:
        std::println("Floppy: Returning 0 for read to port {:4X}", port);
        return 0;
    case NEC765_REG_DOR_RW:
        return dor_;
    case NEC765_REG_STR_R:
        switch (state_) {
        case State::Reset:
            return 0;
        case State::CommandPhase:
            return STR_MASK_RQM;
        case State::CommandArgsPhase:
            return STR_MASK_RQM | STR_MASK_CB;
        case State::ExecutionPhase:
            return STR_MASK_CB;
        case State::ResultPhase:
            return STR_MASK_RQM | STR_MASK_DIO | STR_MASK_CB;
        default:
            throw std::runtime_error { std::format("Floppy: Unsupported read from {:04X}, state = {}", port, (int)state_) };
        }
    case NEC765_REG_DATA_RW: {
        if (state_ != State::ResultPhase)
            throw std::runtime_error { std::format("Floppy: Unsupported read from {:04X} in state {}", port, (int)state_) };
        assert(!result_.empty());
        auto data = result_.front();
        result_.erase(result_.begin());
        if (result_.empty())
            state_ = State::CommandPhase;
        return data;
    }
    default:
        throw std::runtime_error { std::format("Floppy: Unsupported read from {:04X}", port) };
    }
}

void NEC765_FloppyController::impl::outU8(uint16_t port, uint16_t offset, std::uint8_t value)
{
    switch (offset) {
    case NEC765_REG_DOR_RW:
        dor_ = value;
        std::println("Floppy: DOR={:02X}", value);
        if (!(value & DOR_MASK_RESET_N)) {
            std::println("Floppy: Resetting");
            state_ = State::Initial;
        } else if (state_ == State::Initial) {
            std::println("Floppy: Exiting reset");
            state_ = State::Reset;
            setTransition(1000, [&, value]() {
                std::println("Floppy: Reset done");
                reset();
                dor_ = value;
                state_ = State::CommandPhase;
                resetCnt_ = 4;
                raiseIRQ();
            });
        }
        break;
    case NEC765_REG_DATA_RW:
        if (state_ == State::CommandPhase) {
            std::println("Floppy: Command 0x{:02X} ({})", value, CommandName(value));
            command_ = value;
            getCommandArgs();
        } else if (state_ == State::CommandArgsPhase) {
            assert(argsCnt_);
            commandArgs_.push_back(value);
        } else {
            throw std::runtime_error { std::format("Floppy: Unsupported with to {:04X} value {:02X} -- state = {}", port, value, (int)state_) };
        }
        if (commandArgs_.size() == argsCnt_) {
            try {
                executeCommand();
            } catch (const std::exception& e) {
                std::println("Floppy: {}", e.what());

                state_ = State::ResultPhase;
                result_.clear();
                setSt0(1<<6);
                result_.push_back(st0_);
                result_.push_back(1 << 2); // ST1 (Bit 2 = No Data)
                result_.push_back(0); // ST2
                result_.push_back(0); // dr.cylinder
                result_.push_back(0); // dr.head
                result_.push_back(0); // dr.sector
                result_.push_back(2); // N (sector size 512)
                raiseIRQ();

            }
        }
        break;
    case NEC765_REG_RESERVED:
        // This is actually connected to the HDC (FIXED disk controller data register)
        std::println("Floppy: Warning write to reserved register value {:02X}", value);
        break;
    default:
        throw std::runtime_error { std::format("Floppy: Unsupported with to {:04X} value {:02X}", port, value) };
    }
}

void NEC765_FloppyController::impl::getCommandArgs()
{
    commandArgs_.clear();
    switch (command_ & CMD_MASK) {
    case CMD_SPECIFY:
        argsCnt_ = 2;
        break;
    case CMD_SENSE_DRIVE_STATUS:
        argsCnt_ = 1;
        break;
    case CMD_READ_DATA:
        argsCnt_ = 8;
        break;
    case CMD_RECALIBRATE:
        argsCnt_ = 1;
        break;
    case CMD_SEEK:
        argsCnt_ = 2;
        break;
    default:
        argsCnt_ = 0;
    }
    state_ = State::CommandArgsPhase;
}

 void NEC765_FloppyController::impl::setSt0(uint8_t info)
 {
    assert((info & ~(ST0_MASK_IC | ST0_MASK_SE)) == 0);
    const auto& drive = driveState_[curDrive_];
    st0_ = info | (drive.head ? ST0_MASK_HEAD : 0) | curDrive_;
}

void NEC765_FloppyController::impl::executeCommand()
{
    std::string argsString;
    for (auto a : commandArgs_)
        argsString += std::format(" {:02X}", a);

    result_.clear();
    switch (command_ & CMD_MASK) {
    case CMD_SPECIFY:
        std::println("Floppy: SPECIFY {}", argsString);
        if (commandArgs_[1] & 1)
            throw std::runtime_error { std::format("Floppy: Unsupported command 0x{:02X} 0b{:b} ({}){} - Non DMA mode", command_, command_, CommandName(command_), argsString) };
        break;
    case CMD_SENSE_DRIVE_STATUS: {
        const auto dr = commandArgs_[0] & 3;
        const auto& ds = driveState_[dr];
        auto st3 = static_cast<uint8_t>(dr | 1 << 5 | ds.head << 2 | (ds.cylinder == 0 ? 1 << 4 : 0));
        std::println("Floppy: DRIVE STATUS {}: {:02X} 0b{:08b}", argsString, st3, st3);
        result_.push_back(st3);
        break;
    }
    case CMD_READ_DATA: {
        std::println("Floppy: READ_DATA {}. HD={}, DR={} C={} / H={} / S={}", argsString, (commandArgs_[0] >> 3) & 1, commandArgs_[0] & 3, commandArgs_[1], commandArgs_[2], commandArgs_[3]);
        if (curDrive_ != (commandArgs_[0] & 3))
            throw std::runtime_error { std::format("Floppy: Unsupported command 0x{:02X} 0b{:b} ({}){} - Wrong drive {} - expected", command_, command_, CommandName(command_), argsString, curDrive_) };
        auto& dr = driveState_[curDrive_];
        dr.head = commandArgs_[2]; // XXX
        if ((dr.head != (commandArgs_[0] & 4) >> 2) || dr.cylinder != commandArgs_[1] || dr.head != commandArgs_[2]) {
            // XXX: Bochs doesn't put in the 
            const auto msg = std::format("Floppy: Unsupported command 0x{:02X} 0b{:b} ({}){} - Wrong CH - {}/{}, expected {}/{}", command_, command_, CommandName(command_), argsString, commandArgs_[1], commandArgs_[2], dr.cylinder, dr.head);
            std::println("{}", msg);
            std::println("Floppy HACK: Auto seeking");
            dr.cylinder = commandArgs_[1];
            dr.head = commandArgs_[2];
            //throw std::runtime_error { msg };
        }
        if (commandArgs_[4] != 2 || commandArgs_[7] != 0xff)
            throw std::runtime_error { std::format("Floppy: Unsupported command 0x{:02X} 0b{:b} ({}){} - Invalid sector size/data length", command_, command_, CommandName(command_), argsString) };
        const auto& fmt = diskData_[curDrive_].format;
        if (commandArgs_[3] == 0 || commandArgs_[3] > fmt.sectorsPerTrack)
            throw std::runtime_error { std::format("Floppy: Unsupported command 0x{:02X} 0b{:b} ({}){} - Invalid sector {} (max {})", command_, command_, CommandName(command_), argsString, commandArgs_[3], fmt.sectorsPerTrack) };
        state_ = State::ExecutionPhase;
        dr.sector = commandArgs_[3];
        dr.sectorOffset = 0;
        onDmaStart_(false, *this);
        return;
    }
    case CMD_RECALIBRATE:
        std::println("Floppy: RECALIBRATE {}", argsString);
        state_ = State::ExecutionPhase;
        setTransition(1000, [&]() {
            state_ = State::CommandPhase;
            curDrive_ = commandArgs_[0] & 3;
            driveState_[curDrive_].cylinder = 0;
            driveState_[curDrive_].head = 0;
            std::println("Floppy: Drive {} recalibrated", curDrive_);
            setSt0(ST0_MASK_SE);
            raiseIRQ();
        });
        return;
    case CMD_SENSE_INTERRUPT:
        if (resetCnt_) {
            const auto drive = static_cast<uint8_t>(4 - resetCnt_);
            std::println("Floppy: Reset result for drive {}", drive);
            result_.push_back(0xC0 | drive);
            result_.push_back(driveState_[drive].cylinder);
            resetCnt_--;
        } else {
            result_.push_back(st0_);
            result_.push_back(driveState_[curDrive_].cylinder);
            // st0_ = 0;
        }
        break;
    case CMD_SEEK: {
        std::println("Floppy: SEEK {}", argsString);
        state_ = State::ExecutionPhase;
        const auto& fmt = diskData_[curDrive_].format;
        if (((commandArgs_[0] & 4) && fmt.headsPerCylinder < 2) || commandArgs_[1] >= fmt.numCylinder) {
            std::println("Floppy: Unsupported command 0x{:02X} 0b{:b} ({}){} - Invalid seek (disk format {}/{}/{})", command_, command_, CommandName(command_), argsString, fmt.headsPerCylinder, fmt.numCylinder, fmt.sectorsPerTrack);
            commandArgs_[0] &= ~4;
            commandArgs_[1] = static_cast<uint8_t>(fmt.numCylinder - 1);
        }
        setTransition(1000, [&]() {
            state_ = State::CommandPhase;
            curDrive_ = commandArgs_[0] & 3;
            driveState_[curDrive_].head = (commandArgs_[0] & 4) != 0;
            driveState_[curDrive_].cylinder = commandArgs_[1];
            std::println("Floppy: Drive {} SEEK cyl={} head={}", curDrive_, driveState_[curDrive_].cylinder, driveState_[curDrive_].head);
            setSt0(ST0_MASK_SE);
            raiseIRQ();
        });
        return;
    }
    default:
        throw std::runtime_error { std::format("Floppy: Unsupported command 0x{:02X} 0b{:b} ({}){}", command_, command_, CommandName(command_), argsString) };
    }

    if (!result_.empty()) {
        std::println("Floppy: Result phase {} bytes", result_.size());
        state_ = State::ResultPhase;
    } else {
        state_ = State::CommandPhase;
    }
}

uint8_t NEC765_FloppyController::impl::dmaGetU8()
{
    assert(state_ == State::ExecutionPhase);
    assert((command_ & CMD_MASK) == CMD_READ_DATA);
    auto& dr = driveState_[curDrive_];


    const auto& fmt = diskData_[curDrive_].format;

    if (!fmt.validCHS(dr.cylinder, dr.head, dr.sector))
        throw std::runtime_error { std::format("Floppy: Read outside disk area {}/{}/{} (format {}/{}/{})", dr.head, dr.cylinder, dr.sector, fmt.headsPerCylinder, fmt.numCylinder, fmt.sectorsPerTrack) };

    const uint8_t data = diskData_[curDrive_].data[fmt.toLBA(dr.cylinder, dr.head, dr.sector) * bytesPerSector + dr.sectorOffset];
    //std::println("Floppy: Reading {}/{}/{} offset {} - {:02x}", dr.cylinder, dr.head, dr.sector, dr.sectorOffset, data);

    if (++dr.sectorOffset == bytesPerSector) {
        dr.sectorOffset = 0;
        ++dr.sector;
    }

    return data;
}

void NEC765_FloppyController::impl::dmaPutU8(uint8_t)
{
    throw std::runtime_error { "NEC765_FloppyController::impl::dmaPutU8 not implemented" };
}

void NEC765_FloppyController::impl::dmaDone()
{
    assert(state_ == State::ExecutionPhase);
    assert((command_ & CMD_MASK) == CMD_READ_DATA);
    auto& dr = driveState_[curDrive_];
    std::println("Floppy: {} done", CommandName(command_));
    state_ = State::ResultPhase;
    setSt0(0);
    result_.push_back(st0_);
    result_.push_back(0); // ST1
    result_.push_back(0); // ST2
    result_.push_back(dr.cylinder);
    result_.push_back(dr.head);
    result_.push_back(dr.sector);
    result_.push_back(2); // N (sector size 512)
    raiseIRQ();
}

NEC765_FloppyController::NEC765_FloppyController(SystemBus& bus, const OnInterrupt& onInt, const OnDmaStart& onDmaStart, bool reducedIORange)
    : impl_ { std::make_unique<impl>(bus, onInt, onDmaStart, reducedIORange) }
{
}

NEC765_FloppyController::~NEC765_FloppyController() = default;

void NEC765_FloppyController::insertDisk(uint8_t drive, const std::vector<uint8_t>& data)
{
    impl_->insertDisk(drive, data);
}

void NEC765_FloppyController::insertDisk(uint8_t drive, std::string_view filename)
{
    impl_->insertDisk(drive, filename);
}