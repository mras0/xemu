#include "ata_controller.h"
#include "disk_data.h"
#include <print>
#include <utility>
#include <cstring>
#include <optional>

#if 0
std::string CPUIPString();
#define LOG(...) std::println("ATA: {} {}", CPUIPString() , std::format(__VA_ARGS__))
#else
#define LOG(...)
#endif

namespace {
enum {
    BASE_REG_DATA_RW = 0,
    BASE_REG_ERROR_R = 1,
    BASE_REG_FEATURES_W = 1,
    BASE_REG_SECTOR_COUNT_RW = 2,
    BASE_REG_LBA_LOW_RW = 3, // Or sectur number
    BASE_REG_LBA_MID_RW = 4, // Or cylinder low
    BASE_REG_LBA_HIGH_RW = 5, // Or cylinder high
    BASE_REG_DRIVE_HEAD_RW = 6, // Drive/head
    BASE_REG_STATUS_R = 7,
    BASE_REG_COMMAND_W = 7
};

constexpr uint8_t DH_MASK_ADDR_MASK = 0xF; // CHS: bits 0-3 of head, LBA: Bits 24-27
constexpr uint8_t DH_MASK_DRV = 1 << 4; // Selects drive number
constexpr uint8_t DH_MASK_ALWAYS1 = 1 << 5 | 1 << 7;
constexpr uint8_t DH_MASK_LBA = 1 << 6;

enum {
    CONTROL_REG_ALT_STATUS_R = 0, // A duplicate of the Status Register which does not affect interrupts
    CONTROL_REG_DEVICE_CONTROL_W = 0, // Used to reset the bus or enable/disable interrupts
    CONTROL_REG_DIRVE_ADDRESS_R = 1, // Provides drive select and head select information
};

constexpr uint8_t DC_MASK_nIEN = 1 << 1; // Interruple disable
constexpr uint8_t DC_MASK_SRST = 1 << 2; // Software reset (Set then clear)
constexpr uint8_t DC_MASK_HS3E = 1 << 3; // Head select 3 eanble (?)
constexpr uint8_t DC_MASK_HOB = 1 << 7; // High Order Byte

constexpr uint8_t STATUS_MASK_ERR = 1 << 0; // Indicates an error occurred
constexpr uint8_t STATUS_MASK_IDX = 1 << 1; // Index
constexpr uint8_t STATUS_MASK_CORR = 1 << 2; // Corrected data
constexpr uint8_t STATUS_MASK_DRQ = 1 << 3; // Set when the drive has PIO data to transfer, or is ready to accept PIO data. 
constexpr uint8_t STATUS_MASK_SRV = 1 << 4; // Overlapped Mode Service Request. 
constexpr uint8_t STATUS_MASK_DF = 1 << 5; // Drive fault (does not set ERR)
constexpr uint8_t STATUS_MASK_RDY = 1 << 6; // Ready (cleared after an error)
constexpr uint8_t STATUS_MASK_BSY = 1 << 7; // Busy

enum : uint8_t {
    ATA_CMD_READ_SECTORS_WITH_RETRY = 0x20,
    ATA_CMD_READ_SECTORS = 0x21,
    ATA_CMD_WRITE_SECTORS_WITH_RETRY = 0x30,
    ATA_CMD_WRITE_SECTORS = 0x31,
    ATA_CMD_READ_VERIFY_SECTORS = 0x40,
    ATA_CMD_INITIALIZE_DEVICE_PARAMETERS = 0x91,
    ATA_CMD_IDENTIFY_PACKET_DEVICE = 0xA1,
    ATA_CMD_IDENTIFY_DRIVE = 0xEC,
};

enum ErrorCode : uint8_t {
    None = 0b001
};

bool IsWriteCommand(std::uint8_t command)
{
    switch (command) {
    case ATA_CMD_WRITE_SECTORS_WITH_RETRY: // 30
    case ATA_CMD_WRITE_SECTORS: // 31
        return true;
    default:
        return false;
    }
}

std::string CommandString(std::uint8_t command)
{
    if ((command & 0xF0) == 0x10)
        return "Recalibrate (" + HexString(command) + ")";
    switch (command) {
    case ATA_CMD_READ_SECTORS_WITH_RETRY: // 20
        return "Read sector(s)";
    case ATA_CMD_READ_SECTORS: // 21
        return "Read sector(s) w/o retry";
    case ATA_CMD_WRITE_SECTORS_WITH_RETRY: // 30
        return "Write sector(s)";
    case ATA_CMD_WRITE_SECTORS: // 31
        return "Write sector(s) w/o retry";
    case ATA_CMD_READ_VERIFY_SECTORS: // 40:
        return "Read verify sector(s)";
    case ATA_CMD_INITIALIZE_DEVICE_PARAMETERS: // 91
        return "Initialize device parameters";
    case ATA_CMD_IDENTIFY_PACKET_DEVICE: // A1
        return "Identify packet device";
    case ATA_CMD_IDENTIFY_DRIVE: // EC
        return "Identify drive";
    default:
        return std::format("Unknown ATA command {:02X}", command);
    }
}

} // unnamed namespace

class ATAController::impl : public IOHandler, public CycleObserver {
public:
    impl(SystemBus& bus, uint16_t baseRegister, uint16_t controlRegister, onIrqType onIrq);

    void reset();

    std::uint8_t inU8(std::uint16_t port, std::uint16_t offset) override;
    std::uint16_t inU16(std::uint16_t port, std::uint16_t offset) override;
    std::uint32_t inU32(std::uint16_t port, std::uint16_t offset) override;
    void outU8(std::uint16_t port, std::uint16_t offset, std::uint8_t value) override;
    void outU16(std::uint16_t port, std::uint16_t offset, std::uint16_t value) override;
    void outU32(std::uint16_t port, std::uint16_t offset, std::uint32_t value) override;

    void runCycles(std::uint64_t numCycles) override;
    std::uint64_t nextAction() override;

    void insertDisk(uint8_t driveNum, std::string_view filename);
    const DiskFormat& diskFormat(uint8_t driveNum) const
    {
        assert(driveNum < 2);
        return drives_[driveNum].data.format();
    }

private:
    SystemBus& bus_;
    const uint16_t baseRegister_;
    onIrqType onIRQ_;
    uint8_t driveHead_;
    uint8_t deviceControl_;
    uint8_t* dataPtr_;
    uint32_t bytesRemaining_;
    uint32_t cycleCountdown_;
    uint8_t currentCommand_;
    uint8_t error_;
    std::function<void(void)> nextTransition_;
    struct Drive {
        void reset()
        {
            status = STATUS_MASK_RDY;
            if (present()) {
                // Put device ID in registers
                sectorCount = 1;
                lba = 1; // cyl low/cyl high has signature, should probably be 0xEB1401 for ATAPI
            } else {
                sectorCount = 0;
                lba = 0;
            }
            writeOffset = 0;
            writeCount = 0;
        }

        uint8_t status;
        uint16_t sectorCount;
        uint32_t lba;

        size_t writeOffset, writeCount;
        DiskData data;

        bool present() const
        {
            return data.sizeInBytes() != 0;
        }

        uint8_t sectorNumber() const
        {
            return static_cast<uint8_t>(lba);
        }

        uint16_t cylinderNumber() const
        {
            return static_cast<uint16_t>(lba >> 8);
        }

        uint32_t lbaAddress(uint8_t driveHead) const
        {
            assert(driveHead & DH_MASK_LBA);
            return lba | (driveHead & DH_MASK_ADDR_MASK) << 24;
        }

        std::string addressDesc(uint8_t driveHead) const
        {
            if (driveHead & DH_MASK_LBA)
                return std::format("LBA 0x{:X}", lbaAddress(driveHead));
            else
                return std::format("CHS {}/{}/{}", cylinderNumber(), driveHead & DH_MASK_ADDR_MASK, sectorNumber());
        }

        std::optional<size_t> dataOffset(uint8_t driveHead)
        {
            uint32_t addr;
            if (!sectorCount)
                return {};
            if (driveHead & DH_MASK_LBA) {
                addr = lbaAddress(driveHead);
            } else {
                const auto c = cylinderNumber();
                const auto h = static_cast<uint8_t>(driveHead & DH_MASK_ADDR_MASK);
                const auto s = sectorNumber();
                if (!data.format().validCHS(c, h, s))
                    return {};
                addr = data.format().toLBA(c, h, s);
            }
            if (addr >= data.format().totalSectors() || sectorCount > data.format().totalSectors() - addr)
                return {};
            writeOffset = addr * bytesPerSector;
            writeCount = sectorCount * bytesPerSector;
            return writeOffset;
        }
    } drives_[2];
    Drive* commandDrive_;
    std::vector<uint8_t> tempBuf_;

    Drive& selectedDrive()
    {
        return drives_[driveHead_ & DH_MASK_DRV ? 1 : 0];
    }

    bool isControlRegister(std::uint16_t port) const
    {
        return (port & 0xfff8) != baseRegister_;
    }

    void setTransition(const std::function<void(void)>& func, uint32_t commandTime = 1000)
    {
        if (cycleCountdown_)
            throw std::runtime_error{std::format("Command already active")};
        cycleCountdown_ = commandTime;
        nextTransition_ = func;
        bus_.recalcNextAction();
    }

    using CommandFuncType = void (impl::*)(Drive&);
    void startCommand(CommandFuncType commandFunc);

    void cmdIdentifyDrive(Drive& drive);
    void cmdReadWriteSectors(Drive& drive);
    void cmdIgnore(Drive& drive);

    uint32_t doRead(uint8_t numBytes);
    void doWrite(uint8_t numBytes, uint32_t value);
    void setError(ErrorCode err);
    void triggerIrq();
    void commandDone();
};

ATAController::impl::impl(SystemBus& bus, uint16_t baseRegister, uint16_t controlRegister, onIrqType onIrq)
    : bus_ { bus } 
    , baseRegister_ { baseRegister }
    , onIRQ_ { onIrq }
{
    bus.addIOHandler(baseRegister, 8, *this, true);
    bus.addIOHandler(baseRegister | 0xe, 2, *this, true);
    bus.addIOHandler(controlRegister, 2, *this, true);
    bus.addCycleObserver(*this);
    reset();
}

void ATAController::impl::reset()
{
    driveHead_ = 0;
    deviceControl_ = DC_MASK_nIEN;
    dataPtr_ = 0;
    bytesRemaining_ = 0;
    cycleCountdown_ = 0;
    nextTransition_ = {};
    currentCommand_ = 0;
    commandDrive_ = nullptr;
    for (auto& dr : drives_) {
        dr.reset();
    }
    setError(ErrorCode::None);
}

void ATAController::impl::runCycles(std::uint64_t numCycles)
{
    if (!cycleCountdown_)
        return;
    if (numCycles < cycleCountdown_) {
        cycleCountdown_ -= static_cast<uint32_t>(numCycles);
        return;
    }
    assert(nextTransition_);
    cycleCountdown_ = 0;
    std::exchange(nextTransition_, {})();
}

std::uint64_t ATAController::impl::nextAction()
{
    return cycleCountdown_ ? cycleCountdown_: UINT64_MAX;
}

void ATAController::impl::insertDisk(uint8_t driveNum, std::string_view filename)
{
    assert(driveNum < 2);
    auto& data = drives_[driveNum].data;
    if (filename.empty()) {
        data.eject();
    } else {
        data.insert(filename);
        LOG("{} Inserting {} {}/{}/{} {} MB", driveNum, filename, data.format().numCylinder, data.format().headsPerCylinder, data.format().sectorsPerTrack, data.format().sizeInBytes() / (1024. * 1024));
    }
}

std::uint8_t ATAController::impl::inU8(std::uint16_t port, std::uint16_t offset)
{
    if (isControlRegister(port) || offset != BASE_REG_STATUS_R) {
        LOG("IN port={:04X}", port);
    }

    if (isControlRegister(port)) {
        if (offset == 0)
            return deviceControl_;
        else if (offset == 1) {
            //THROW_FLIPFLOP();
            return 0x7C | (driveHead_ & DH_MASK_DRV ? 1 : 2);
        }

        LOG("TODO inu8 offset {} from control port", offset);
        return IOHandler::inU8(port, offset);
    }

    auto& dr = selectedDrive();

    switch (offset) {
    case BASE_REG_ERROR_R:
        return 0; // error_;
    case BASE_REG_SECTOR_COUNT_RW: // 2
        return static_cast<uint8_t>(dr.sectorCount);
    case BASE_REG_LBA_LOW_RW: // 3
        return static_cast<uint8_t>(dr.lba);
    case BASE_REG_LBA_MID_RW: // 4
        return static_cast<uint8_t>(dr.lba >> 8);
    case BASE_REG_LBA_HIGH_RW: // 5
        return static_cast<uint8_t>(dr.lba >> 16);
    case BASE_REG_DRIVE_HEAD_RW:
        return driveHead_;
    case BASE_REG_STATUS_R: // 7
        onIRQ_(false);
        return dr.status | (bytesRemaining_ ? STATUS_MASK_DRQ : 0);
    }

    LOG("TODO inu8 offset {}", offset);
    return IOHandler::inU8(port, offset);
}

void ATAController::impl::outU8(std::uint16_t port, std::uint16_t offset, std::uint8_t value)
{
    LOG("OUT port={:04X} value={:02X}", port, value);
    if (isControlRegister(port)) {
        if (offset == 0) {
            // Device control register
            LOG("Device control register write {:02X} (nIEN = {})", value, value & DC_MASK_nIEN ? 1 : 0);
            if (value & ~(DC_MASK_HS3E | DC_MASK_nIEN | DC_MASK_SRST))
                throw std::runtime_error { std::format("Unsupported write to ATA device control register: {:02X}", value) };
            if ((deviceControl_ ^ value) & DC_MASK_SRST) {
                // XXX: Actually both are reset!
                LOG("{} reset", value & DC_MASK_SRST ? "Entering" : "Exiting");
                for (auto& dr : drives_) {
                    if (value & DC_MASK_SRST) {
                        dr.status |= STATUS_MASK_BSY;
                        dr.status &= ~STATUS_MASK_RDY;
                    } else {
                        assert(dr.status & STATUS_MASK_BSY);
                        dr.reset();
                    }
                }
            }
            deviceControl_ = value;
            return;
        }
        LOG("TODO outu8 offset {} to control port value {:02X}", offset, value);
        return IOHandler::outU8(port, offset, value);
    }

    auto& dr = selectedDrive();

    switch (offset) {
    case BASE_REG_FEATURES_W:
        if (value == 0)
            return;
        break;
    case BASE_REG_SECTOR_COUNT_RW: // 2
        // 0 means 256, but OS/2 installer expects less than 7-bits to be possible
        //dr.sectorCount = value & 0x3f;
        if (value == 0) {
            LOG("Warning: sector count usually means 256 sectors");
            THROW_FLIPFLOP();
        }
        dr.sectorCount = value;
        return;
    case BASE_REG_LBA_LOW_RW: // 3
        dr.lba = (dr.lba & 0xffffff00) | value;
        return;
    case BASE_REG_LBA_MID_RW: // 4
        dr.lba = (dr.lba & 0xffff00ff) | value << 8;
        return;
    case BASE_REG_LBA_HIGH_RW: // 5
        dr.lba = (dr.lba & 0xff00ffff) | value << 16;
        return;
    case BASE_REG_DRIVE_HEAD_RW: // 6
        LOG("Drive/head {:02X} DRV={} LBA={}", value, (value & DH_MASK_DRV) ? 1 : 0, (value & DH_MASK_LBA) ? 1 : 0);
        driveHead_ = value;
        return;
    case BASE_REG_COMMAND_W:
        setError(ErrorCode::None);
        dr.status &= ~(STATUS_MASK_ERR | STATUS_MASK_SRV);
        currentCommand_ = value;
        LOG("Command: {} sectorCount = {} {}", CommandString(value), dr.sectorCount, dr.addressDesc(driveHead_));
        if ((value & 0xF0) == 0x10) {
            startCommand(&impl::cmdIgnore);
            return;
        }
        switch (value) {
        case ATA_CMD_READ_SECTORS_WITH_RETRY:
        case ATA_CMD_READ_SECTORS:
        case ATA_CMD_WRITE_SECTORS_WITH_RETRY:
        case ATA_CMD_WRITE_SECTORS:
            startCommand(&impl::cmdReadWriteSectors);
            return;
        case ATA_CMD_READ_VERIFY_SECTORS:
            startCommand(&impl::cmdIgnore);
            return;
        case ATA_CMD_INITIALIZE_DEVICE_PARAMETERS: {
            const auto& fmt = dr.data.format();
            const uint8_t h = static_cast<uint8_t>(driveHead_ & DH_MASK_ADDR_MASK) + 1;
            if (dr.sectorCount != fmt.sectorsPerTrack || h != fmt.headsPerCylinder) {
                std::println("ATA: {} {}/{} doesn't match {}", CommandString(currentCommand_), h, dr.sectorCount, fmt.headsPerCylinder, fmt.sectorsPerTrack);
                THROW_FLIPFLOP();
            }
            startCommand(&impl::cmdIgnore);
            return;
        }
        case ATA_CMD_IDENTIFY_PACKET_DEVICE: // 0xA1
            dr.status |= STATUS_MASK_ERR;
            return;
        case ATA_CMD_IDENTIFY_DRIVE: // 0xEC
            startCommand(&impl::cmdIdentifyDrive);
            return;
        default:
            std::println("ATA: Unsupported command {} sectorCount = {} {}", CommandString(value), dr.sectorCount, dr.addressDesc(driveHead_));
            THROW_FLIPFLOP();
            dr.status |= STATUS_MASK_ERR;
        }
        break;
    }

    LOG("TODO outu8 offset {} value {:02X}", offset, value);
    IOHandler::outU8(port, offset, value);
}

uint32_t ATAController::impl::doRead(uint8_t numBytes)
{
    if (bytesRemaining_ < numBytes || IsWriteCommand(currentCommand_))
        throw std::runtime_error { std::format("ATA: Unexpected read from data port size={} bytesRemaining_={:X} {}", numBytes, bytesRemaining_, CommandString(currentCommand_)) };
    assert(dataPtr_ && commandDrive_);
    assert(numBytes == 2 || numBytes == 4);
    const auto res = numBytes == 2 ? GetU16(dataPtr_) : GetU32(dataPtr_);
    dataPtr_ += numBytes;
    bytesRemaining_ -= numBytes;
    if (!bytesRemaining_) {
        dataPtr_ = nullptr;
        commandDrive_ = nullptr;
        commandDone();
    } else if (bytesRemaining_ % bytesPerSector == 0) {
        if (!cycleCountdown_)
            setTransition([&]() { triggerIrq(); });
        else
            LOG("Warning transition already active!");
    }
    return res;
}

std::uint16_t ATAController::impl::inU16(std::uint16_t port, std::uint16_t offset)
{
    if (offset != BASE_REG_DATA_RW)
        throw std::runtime_error { std::format("ATA: 16-bit input not supported port={:04X} offset={:02X} (bytes remaining {}) command = {}", port, offset, bytesRemaining_, CommandString(currentCommand_)) };
    return static_cast<uint16_t>(doRead(2));
}

std::uint32_t ATAController::impl::inU32(std::uint16_t port, std::uint16_t offset)
{
    if (offset != BASE_REG_DATA_RW)
        throw std::runtime_error { std::format("ATA: 32-bit input not supported port={:04X} offset={:02X}", port, offset) };
    return doRead(4);
}

void ATAController::impl::doWrite(uint8_t numBytes, uint32_t value)
{
    if (bytesRemaining_ < numBytes || !IsWriteCommand(currentCommand_))
        throw std::runtime_error { std::format("ATA: Unexpected write to data port size={} value={:04X}, bytesRemaining_={:X} {}", numBytes, value, bytesRemaining_, CommandString(currentCommand_)) };
    assert(dataPtr_ && commandDrive_);
    assert(numBytes == 2 || numBytes == 4);
    if (numBytes == 2)
        PutU16(dataPtr_, static_cast<uint16_t>(value));
    else
        PutU32(dataPtr_, value);
    dataPtr_ += numBytes;
    bytesRemaining_ -= numBytes;
    if (bytesRemaining_ % bytesPerSector == 0) {
        if (!cycleCountdown_)
            setTransition([&]() { triggerIrq(); });
        else
            LOG("Warning transition already active!");
    }
        
    if (!bytesRemaining_) {
        dataPtr_ = nullptr;
        assert(commandDrive_->writeCount);
        commandDrive_->data.write(&tempBuf_[0], commandDrive_->writeOffset, commandDrive_->writeCount);
        commandDrive_->writeOffset = 0;
        commandDrive_->writeCount = 0;
        commandDrive_ = nullptr;
        commandDone();
    }
}

void ATAController::impl::outU16(std::uint16_t port, std::uint16_t offset, std::uint16_t value)
{
    if (offset != BASE_REG_DATA_RW)
        throw std::runtime_error { std::format("ATA: 16-bit output not supported port={:04X} offset={:02X} (bytes remaining {}) command = {}", port, offset, bytesRemaining_, CommandString(currentCommand_)) };
    doWrite(2, value);
}

void ATAController::impl::outU32(std::uint16_t port, std::uint16_t offset, std::uint32_t value)
{
    if (offset != BASE_REG_DATA_RW)
        throw std::runtime_error { std::format("ATA: 32-bit output not supported port={:04X} offset={:02X} value={:X}", port, offset, value) };
    doWrite(4, value);
}


void ATAController::impl::startCommand(CommandFuncType commandFunc)
{
    auto& dr = selectedDrive();
    assert(!(dr.status & STATUS_MASK_BSY));
    dr.status |= STATUS_MASK_BSY;
    dr.status &= ~STATUS_MASK_RDY;
    setTransition([this, &dr, commandFunc]() {
        assert(dr.status & STATUS_MASK_BSY);
        dr.status &= ~STATUS_MASK_BSY;
        dr.status |= STATUS_MASK_RDY | STATUS_MASK_SRV;
        commandDrive_ = &dr;
        (*this.*commandFunc)(dr);
    });
}

void ATAController::impl::cmdIdentifyDrive(Drive& drive)
{
    assert(!dataPtr_ && !bytesRemaining_);
    tempBuf_.resize(bytesPerSector);
    dataPtr_ = &tempBuf_[0];
    bytesRemaining_ = bytesPerSector;
    std::memset(&tempBuf_[0], 0, bytesRemaining_);

    auto putWord = [&](uint32_t wordIndex, uint16_t value) {
        assert(wordIndex < 256);
        PutU16(&tempBuf_[wordIndex * 2], value);
    };
    auto putDword = [&](uint32_t wordIndex, uint32_t value) {
        putWord(wordIndex, static_cast<uint16_t>(value));
        putWord(wordIndex + 1, static_cast<uint16_t>(value >> 16));
    };
    auto putString = [&](uint32_t wordIndex, uint32_t byteSize, std::string_view text) {
        assert(byteSize % 2 == 0);
        assert(wordIndex < 256 && wordIndex + byteSize/2 <= 256);
        assert(text.length() <= byteSize);
        uint8_t* dest = &tempBuf_[wordIndex * 2];
        std::memset(dest, ' ', byteSize);
        for (uint32_t i = 0; i < std::min(byteSize, static_cast<uint32_t>(text.length())); ++i)
            dest[i ^ 1] = text[i];
    };

    const auto& fmt = drive.data.format();

    putWord(0, 1 << 6); // General configuration, 6 = Fixed Disk
    putWord(1, static_cast<uint16_t>(fmt.numCylinder));
    putWord(3, static_cast<uint16_t>(fmt.headsPerCylinder));
    putWord(4, /*static_cast<uint16_t>(fmt.sectorsPerTrack * bytesPerSector)*/0);
    putWord(5, static_cast<uint16_t>(bytesPerSector));
    putWord(6, static_cast<uint16_t>(fmt.sectorsPerTrack));
    putString(10, 20, "SerialNo");
    putString(23, 8, "FirmwRev");
    putString(27, 40, "Model number!!");
    putWord(47, 16); // max sectors per interrupt
    putWord(48, 1); // bit0 = double word IO supported
    putWord(49, 1 << 9 /*| 1 << 8*/); // bit9 = LBA supported, bit8 = DMA supported
    putWord(53, 7); // Mark 54-58 / 64-70 as valid
    putWord(54, static_cast<uint16_t>(fmt.numCylinder));
    putWord(55, static_cast<uint16_t>(fmt.headsPerCylinder));
    putWord(56, static_cast<uint16_t>(fmt.sectorsPerTrack));
    putDword(57, fmt.totalSectors());
    putWord(59, 0); // bit 8 = multiple sector command valid, bit 7-0 = max sectors supported for mulitple r/w
    putDword(60, fmt.totalSectors());
 //   putWord(80, 1 << 2 | 1 << 1); // Major version = ATA-2 (bit 1 = ATA-1, bit 2 = ATA-2, etc.)
//    HexDump(0, dataPtr_, 512);
    triggerIrq();
    commandDone();
}

void ATAController::impl::cmdReadWriteSectors(Drive& drive)
{
    //assert(!dataPtr_ && !bytesRemaining_);
    //dataPtr_ = drive.dataPtr(driveHead_);
    auto offset = drive.dataOffset(driveHead_);
    if (!offset)
        throw std::runtime_error { std::format("TODO: {} invalid sectorCount = {} {}", CommandString(currentCommand_), drive.sectorCount, drive.addressDesc(driveHead_)) };
    bytesRemaining_ = bytesPerSector * drive.sectorCount;
    tempBuf_.resize(bytesRemaining_);
    dataPtr_ = &tempBuf_[0];
    if (!IsWriteCommand(currentCommand_)) {
        drive.writeCount = 0;
        drive.data.read(&tempBuf_[0], *offset, bytesRemaining_);
        triggerIrq();
    }
}

void ATAController::impl::cmdIgnore(Drive& drive)
{
    drive.status &= ~(STATUS_MASK_DF | STATUS_MASK_ERR);
    triggerIrq();
    commandDone();
}

void ATAController::impl::setError(ErrorCode err)
{
    error_ = (driveHead_ & DH_MASK_DRV ? 0x80 : 0x00) | static_cast<uint8_t>(err);
}

void ATAController::impl::triggerIrq()
{
    if (!(deviceControl_ & DC_MASK_nIEN)) {
        LOG("Triggering IRQ");
        onIRQ_(true);
    }
}

void ATAController::impl::commandDone()
{
    LOG("ATA: {} done, status = {:02X} {}", CommandString(currentCommand_), selectedDrive().status | (bytesRemaining_ ? STATUS_MASK_DRQ : 0), deviceControl_ & DC_MASK_nIEN ? "" : "triggering IRQ");
    currentCommand_ = 0;
}

ATAController::ATAController(SystemBus& bus, uint16_t baseRegister, uint16_t controlRegister, onIrqType onIrq)
    : impl_{ std::make_unique<impl>(bus, baseRegister, controlRegister, onIrq) }
{
}

ATAController::~ATAController() = default;

void ATAController::insertDisk(uint8_t driveNum, std::string_view filename)
{
    impl_->insertDisk(driveNum, filename);
}

const DiskFormat& ATAController::diskFormat(uint8_t driveNum) const
{
    return impl_->diskFormat(driveNum);
}