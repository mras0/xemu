#include "i8042_ps2_controller.h"
#include <print>
#include <cstring>

#define LOG(...) std::println("i8042: " __VA_ARGS__)

enum : uint8_t {
    CMD_DISABLE_PORT2 = 0xA7,
    CMD_ENABLE_PORT2 = 0xA8,
    CMD_TEST_PORT2 = 0xA9,
    CMD_SELF_TEST = 0xAA,
    CMD_TEST_PORT1 = 0xAB,
    CMD_DISABLE_PORT1 = 0xAD,
    CMD_ENABLE_PORT1 = 0xAE,
    CMD_WRITE_CONTROLLER_OUTPUT = 0xD1,
    CMD_WRITE_INPUT_PORT1 = 0xD4,
    CMD_WRITE_PORT2 = 0xD4,
};

enum : uint8_t {
    DEV_RSP_ACK = 0xFA,
    DEV_RSP_RESEND = 0xFE,
};

static constexpr uint8_t STATUS_MASK_OUTPUT = 1 << 0; // 1 = Output buffer full
static constexpr uint8_t STATUS_MASK_INPUT = 1 << 1; // 1 = Input buffer full
static constexpr uint8_t STATUS_MASK_COMMAND = 1 << 3; // 0 = data written to input buffer is data for PS/2 device, 1 = data written to input buffer is data for PS/2 controller command
static constexpr uint8_t STATUS_MASK_PORT2_FULL = 1 << 5;

static constexpr uint8_t CONFIG_MASK_PORT1_IRQ = 1 << 0; // First PS/2 port interrupt (1 = enabled, 0 = disabled) 
static constexpr uint8_t CONFIG_MASK_PORT2_IRQ = 1 << 1; // Second PS/2 port interrupt (1 = enabled, 0 = disabled, only if 2 PS/2 ports supported) 
static constexpr uint8_t CONFIG_MASK_SYSTEM = 1 << 2; // System Flag (1 = system passed POST)
// Bit 3 should be zero
static constexpr uint8_t CONFIG_MASK_PORT1_CLOCK_DISABLE = 1 << 4; // First PS/2 port clock (1 = disabled, 0 = enabled) 
static constexpr uint8_t CONFIG_MASK_PORT2_CLOCK_DISABLE = 1 << 5; // Second PS/2 port clock (1 = disabled, 0 = enabled, only if 2 PS/2 ports supported) 
static constexpr uint8_t CONFIG_MASK_PORT1_TRANSLATE = 1 << 6; // First PS/2 port translation (1 = enabled, 0 = disabled)
// Bit 7 must be zero


static constexpr uint8_t CTRL_OUT_MASK_nRESET = 1 << 0;
static constexpr uint8_t CTRL_OUT_MASK_A20 = 1 << 1;
static constexpr uint8_t CTRL_OUT_MASK_PORT2_CLOCK = 1 << 2;
static constexpr uint8_t CTRL_OUT_MASK_PORT2_DATA = 1 << 3;
static constexpr uint8_t CTRL_OUT_MASK_OUT_FULL_PORT1 = 1 << 4;
static constexpr uint8_t CTRL_OUT_MASK_OUT_FULL_PORT2 = 1 << 5;
static constexpr uint8_t CTRL_OUT_MASK_PORT1_CLOCK = 1 << 6;
static constexpr uint8_t CTRL_OUT_MASK_PORT1_DATA = 1 << 7;

static constexpr uint32_t RAM_SIZE = 0x40;

enum {
    RAM_LOC_CONFIG = 0x20,
    RAM_LOC_INDIRECT = 0x2B,
    RAM_LOC_NONE = 0xff,
};

class i8042_PS2Controller::impl : public IOHandler {
public:
    explicit impl(SystemBus& bus, CallbackType onDevice1IRQ, A20CallbackType onA20CLinehange);

    void reset();

    std::uint8_t inU8(std::uint16_t port, std::uint16_t offset) override;
    void outU8(std::uint16_t port, std::uint16_t offset, std::uint8_t value) override;

    void enqueueKey(const KeyPress& key);
private:
    CallbackType onDevice1IRQ_;
    A20CallbackType onA20CLinehange_;
    uint8_t status_;
    uint8_t portB_;
    std::vector<uint8_t> outputBuffer_;
    uint8_t ram_[RAM_SIZE];
    uint8_t ramWriteOffset_;
    enum WriteDest {
        DEST_PORT1,
        DEST_PORT2,
        DEST_RAM,
        DEST_CTRL_OUTPUT
    } nextDest_;
    uint8_t expectedCommandBytes_;
    uint8_t commandPos_;
    uint8_t port1CommandBytes_[3];
    uint8_t lastData_;

    void checkIrq();
    void enqueueOutputByte(uint8_t data);

    void setNextdest(WriteDest dest)
    {
        assert(nextDest_ == DEST_PORT1);
        assert(dest != DEST_PORT1);
        nextDest_ = dest;
        status_ |= STATUS_MASK_COMMAND;
    }

    void setA20State(bool enabled);
};

i8042_PS2Controller::impl::impl(SystemBus& bus, CallbackType onDevice1IRQ, A20CallbackType onA20CLinehange)
    : onDevice1IRQ_ { onDevice1IRQ }
    , onA20CLinehange_ { onA20CLinehange }
{
    bus.addIOHandler(0x60, 5, *this, true);
    reset();
}

void i8042_PS2Controller::impl::reset()
{
    status_ = 0;
    portB_ = 0;
    outputBuffer_.clear();
    std::memset(ram_, 0, sizeof(ram_));
    ram_[RAM_LOC_INDIRECT] = 0x20;
    ram_[RAM_LOC_CONFIG] = CONFIG_MASK_PORT2_CLOCK_DISABLE;
    ramWriteOffset_ = RAM_LOC_NONE;
    nextDest_ = DEST_PORT1;
    expectedCommandBytes_ = 0;
    lastData_ = 0;
    setA20State(false);
}

void i8042_PS2Controller::impl::checkIrq()
{
    if (((ram_[RAM_LOC_CONFIG] & (CONFIG_MASK_PORT1_IRQ | CONFIG_MASK_PORT1_CLOCK_DISABLE)) == CONFIG_MASK_PORT1_IRQ)
        && !outputBuffer_.empty())
        onDevice1IRQ_();
}

void i8042_PS2Controller::impl::setA20State(bool enabled)
{
    LOG("A20 {}", enabled ? "Enable" : "Disable");
    onA20CLinehange_(enabled);
}

std::uint8_t i8042_PS2Controller::impl::inU8(std::uint16_t port, std::uint16_t offset)
{
    switch (offset) {
    case 0: // Data
        status_ &= ~STATUS_MASK_PORT2_FULL; // XXX
        if (!outputBuffer_.empty()) {
            lastData_ = outputBuffer_.front();
            outputBuffer_.erase(outputBuffer_.begin());
            checkIrq();
            return lastData_;
        } else {
            LOG("Read with empty data buffer!");
            return 0;
        }
    case 1: // KB controller port B control register for compatability with 8255
        portB_ ^= 0x10; // Refresh done (used for delay)
        return portB_;
    case 4: // Status
        return status_ | (outputBuffer_.empty() ? 0 : STATUS_MASK_OUTPUT);
    default:
        LOG("TODO!");
        return IOHandler::inU8(port, offset);
    }
}

void i8042_PS2Controller::impl::outU8(std::uint16_t port, std::uint16_t offset, std::uint8_t value)
{
    switch (offset) {
    case 0: // Data
        if (status_ & STATUS_MASK_COMMAND) {
            switch (nextDest_) {
            case DEST_RAM:
                assert(ramWriteOffset_ != RAM_LOC_NONE);
                LOG("Write to RAM location 0x{:02X} value 0x{:02X}", ramWriteOffset_, value);
                if (ramWriteOffset_ == RAM_LOC_CONFIG)
                    value |= CONFIG_MASK_PORT2_CLOCK_DISABLE; // Do not allow port 2 clock to be enabled
                ram_[ramWriteOffset_] = value;
                ramWriteOffset_ = RAM_LOC_NONE;
                break;
            case DEST_CTRL_OUTPUT:
                LOG("Write to controller output value 0x{:02X} 0b{:08b}", value, value);
                if (value == 0xDD || value == 0xDF) { // Values used by Jemm (FreeDOS)
                    setA20State(!!(value & CTRL_OUT_MASK_A20));
                    break;
                }
                goto destTodo;
            case DEST_PORT2:
                LOG("Ignoring write to port2: {:02X}", value);
                status_ |= STATUS_MASK_PORT2_FULL;
                enqueueOutputByte(0xFE); // RESEND (= no mouse)
                break;
            default:
            destTodo:
                throw std::runtime_error { std::format("TODO: i8042 command data byte: {:02X}, nextDest_ = {}", value, (int)nextDest_) };
            }
            nextDest_ = DEST_PORT1;
            status_ &= ~STATUS_MASK_COMMAND;
            checkIrq();
            return;
        }
        // Data written to device - this enables the device (https://www.os2museum.com/wp/ibm-pcat-8042-keyboard-controller-commands/)
        ram_[RAM_LOC_CONFIG] &= ~CONFIG_MASK_PORT1_CLOCK_DISABLE;

        if (expectedCommandBytes_) {
            port1CommandBytes_[commandPos_++] = value;
            if (--expectedCommandBytes_ == 0) {
                LOG("TODO: Handle device command: {}", HexString(port1CommandBytes_, commandPos_));
            }
            break;
        }

        switch (value) {
        case 0x05:
            LOG("Keyboard - Ignoring command {:02X}", value);
            break;

        case 0xED: // Set LEDs
        case 0xF3: // Set typematic rate and delay
            port1CommandBytes_[0] = value;
            expectedCommandBytes_ = 1;
            commandPos_ = 1;
            break;

        case 0xF2: // Identify keyboard
            LOG("Keyboard - identify");
            enqueueOutputByte(DEV_RSP_ACK);
            break;
        case 0xF4:
            LOG("TODO: Keyboard - enable scanning");
            enqueueOutputByte(DEV_RSP_ACK);
            break;
        case 0xF5:
            LOG("TODO: Keyboard - disable scanning");
            enqueueOutputByte(DEV_RSP_ACK);
            break;
        case 0xFF: // Reset and start self-test
            LOG("Keyboard reset and start self-test");
            enqueueOutputByte(DEV_RSP_ACK); // ACK
            enqueueOutputByte(0xAA); // Self-test passed
            break;
        default:
            LOG("TODO: Device data write {:02X}", value);
            goto todo;
        }
        break;
    case 1: // Port 61h (Port B on XT)
        LOG("Ignoring output to port {:02X} value {:02X}!", port, value);
        break;
    case 4: // Command
        // 00-1Fh: Read RAM indirect
        // 20-3Fh: Read RAM
        // 40-5Fh: Write RAM indirect
        // 60-7Fh: Write RAM
        if (value < 0x80) {
            const auto ramOffset = static_cast<uint8_t>((value & 0x20) ? 0x20 | (value & 0x1f) : ram_[RAM_LOC_INDIRECT] + (value & 0x1f));
            const bool write = (value & 0x40);
            if (ramOffset < RAM_SIZE) {
                LOG("Command: {} RAM offset 0x{:X}", write ? "Write" : "Read", ramOffset);
                if (write) {
                    ramWriteOffset_ = ramOffset;
                    setNextdest(DEST_RAM);
                    return;
                }
                enqueueOutputByte(ram_[ramOffset]);
                break;
            } else {
                LOG("Out of bounds {} to RAM offset 0x{:X}", write ? "write" : "read", ramOffset);
                goto todo;
            }
        }

        switch (value) {
        case CMD_DISABLE_PORT2: // A7
            LOG("Disable port 2");
            ram_[RAM_LOC_CONFIG] |= CONFIG_MASK_PORT2_CLOCK_DISABLE;
            break;
        case CMD_ENABLE_PORT2: // A8
            LOG("Enable port 2");
            ram_[RAM_LOC_CONFIG] &= ~CONFIG_MASK_PORT2_CLOCK_DISABLE;
            break;
        case CMD_TEST_PORT2: // A9
            LOG("TODO: Test port 2");
            goto todo;
            break;
        case CMD_SELF_TEST: // AA
            // https://www.os2museum.com/wp/ibm-pcat-8042-keyboard-controller-commands/
            // [...] the A20 address line is enabled, keyboard interface is disabled, and scan code translation is enabled.
            // [...] the keyboard controller does not start operating until the self test command is sent by the host and successfully completed by the KBC.
            LOG("Self-test");
            reset();
            //setA20State(true);
            enqueueOutputByte(0x55); // Success (0xFC for failed)
            break;
        case CMD_TEST_PORT1: // AB
            LOG("Interface test");
            enqueueOutputByte(0x00); // Test passed (<> 0x00 for various failures)
            break;
        case CMD_DISABLE_PORT1: // AD
            LOG("Disable port 1");
            ram_[RAM_LOC_CONFIG] |= CONFIG_MASK_PORT1_CLOCK_DISABLE;
            break;
        case CMD_ENABLE_PORT1: // AE
            LOG("Enable port 1");
            ram_[RAM_LOC_CONFIG] &= ~CONFIG_MASK_PORT1_CLOCK_DISABLE;
            break;
        case CMD_WRITE_CONTROLLER_OUTPUT: // D1
            LOG("Write controller output port");
            setNextdest(DEST_CTRL_OUTPUT);
            break;
        case CMD_WRITE_PORT2:
            LOG("Write next output to port2");
            setNextdest(DEST_PORT2);
            break;
        case 0xFF:
            LOG("Command FF?? - Used by FreeDos");
            break;
        default:
            throw std::runtime_error { std::format("TODO: i8042 command: {:02X}", value) };
        }
        checkIrq();
        break;
    default:
    todo:
        LOG("TODO Output to port {:02X} value {:02X}!", port, value);
        IOHandler::outU8(port, offset, value);
    }
}

void i8042_PS2Controller::impl::enqueueKey(const KeyPress& key)
{
    // TODO: Use scan set 2?
    std::println("Keyboard event: down={} code={:02X}", key.down ? 1 : 0, key.scanCode);
    if (key.extendedKey)
        enqueueOutputByte(0xE0);
    enqueueOutputByte(static_cast<uint8_t>(key.scanCode | (key.down ? 0x00 : 0x80)));
}

void i8042_PS2Controller::impl::enqueueOutputByte(uint8_t data)
{
    outputBuffer_.push_back(data);
    checkIrq();
}

i8042_PS2Controller::i8042_PS2Controller(SystemBus& bus, CallbackType onDevice1IRQ, A20CallbackType onA20CLinehange)
    : impl_{std::make_unique<impl>(bus, onDevice1IRQ, onA20CLinehange)}
{
}

i8042_PS2Controller::~i8042_PS2Controller() = default;

void i8042_PS2Controller::enqueueKey(const KeyPress& key)
{
    impl_->enqueueKey(key);
}
