#include "i8042_ps2_controller.h"
#include <print>
#include <cstring>

#define LOG(...) std::println("i8042: " __VA_ARGS__)
#define ERROR(...) do { LOG(__VA_ARGS__); THROW_FLIPFLOP(); } while (0)

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

static constexpr uint8_t MOUSE_STATE_MASK_LEFT = 1 << 0;
static constexpr uint8_t MOUSE_STATE_MASK_RIGHT = 1 << 1;
static constexpr uint8_t MOUSE_STATE_MASK_MIDDLE = 1 << 2;
static constexpr uint8_t MOUSE_STATE_MASK_ALWAYS1 = 1 << 3;
static constexpr uint8_t MOUSE_STATE_MASK_XSIGN = 1 << 4;
static constexpr uint8_t MOUSE_STATE_MASK_YSIGN = 1 << 5;
static constexpr uint8_t MOUSE_STATE_MASK_XOVERFLOW = 1 << 6;
static constexpr uint8_t MOUSE_STATE_MASK_YOVERFLOW = 1 << 7;

static constexpr uint32_t RAM_SIZE = 0x40;

enum {
    RAM_LOC_CONFIG = 0x20,
    RAM_LOC_INDIRECT = 0x2B,
    RAM_LOC_NONE = 0xff,
};


static uint8_t popFront(std::vector<uint8_t>& buffer)
{
    assert(!buffer.empty());
    const uint8_t data = buffer.front();
    buffer.erase(buffer.begin());
    return data;
}

class i8042_PS2Controller::impl : public IOHandler {
public:
    explicit impl(SystemBus& bus, CallbackType onDevice1IRQ, CallbackType onDevice2IRQ, A20CallbackType onA20CLinehange);

    void reset();

    std::uint8_t inU8(std::uint16_t port, std::uint16_t offset) override;
    void outU8(std::uint16_t port, std::uint16_t offset, std::uint8_t value) override;

    void enqueueKey(const KeyPress& key);
    void mouseMove(int dx, int dy);
    void mouseButton(int idx, bool down);
    void mouseUpdate();

private:
    CallbackType onDevice1IRQ_;
    CallbackType onDevice2IRQ_;
    A20CallbackType onA20CLinehange_;
    uint8_t status_;
    uint8_t portB_;
    std::vector<uint8_t> outputBuffer_;
    std::vector<uint8_t> dev2OutputBuffer_;
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
    bool device1Command_;
    uint8_t portCommandBytes_[3];

    uint8_t outputByte_;
    uint8_t outputDevice_;

    bool mouseWrapMode_;
    bool mouseDataReporting_;
    int mouseDx_;
    int mouseDy_;
    uint8_t mouseState_;
    bool mouseScaling_; // 2:1 scaling
    uint8_t mouseResolution_;
    uint8_t mouseSampleRate_;


    void checkIrq();
    void enqueueOutputByte(uint8_t data);
    void enqueueDev2OutputByte(uint8_t data);

    void setNextdest(WriteDest dest)
    {
        if (nextDest_ != DEST_PORT1) {
            // assert(nextDest_ == DEST_PORT1); // Bochs set_kbd_command_byte bug(?)
            const char* const desc[4] = { "PORT1", "PORT2", "RAM", "CTRL_OUTPUT" };
            LOG("Warning: changing destination from {} to {}", desc[nextDest_], desc[dest]);
        }
        assert(dest != DEST_PORT1);
        nextDest_ = dest;
        status_ |= STATUS_MASK_COMMAND;
    }

    void setA20State(bool enabled);

    void startCommandWithArgs(uint8_t command, bool device1);
    void device1Command(uint8_t command);
    void device1CommandWithArgs();
    void mouseReset();
    void device2Command(uint8_t command);
    void device2CommandWithArgs();
    void clearMouseData();
    void sendMouseData();
};

i8042_PS2Controller::impl::impl(SystemBus& bus, CallbackType onDevice1IRQ, CallbackType onDevice2IRQ, A20CallbackType onA20CLinehange)
    : onDevice1IRQ_ { onDevice1IRQ }
    , onDevice2IRQ_ { onDevice2IRQ }
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
    dev2OutputBuffer_.clear();
    std::memset(ram_, 0, sizeof(ram_));
    ram_[RAM_LOC_INDIRECT] = 0x20;
    ram_[RAM_LOC_CONFIG] = CONFIG_MASK_PORT2_CLOCK_DISABLE;
    ramWriteOffset_ = RAM_LOC_NONE;
    nextDest_ = DEST_PORT1;
    expectedCommandBytes_ = 0;
    commandPos_ = 0;
    std::memset(portCommandBytes_, 0, sizeof(portCommandBytes_));
    device1Command_ = true;
    outputByte_ = 0;
    outputDevice_ = 0;
    mouseReset();
    setA20State(false);
}

void i8042_PS2Controller::impl::checkIrq()
{
    if (!outputDevice_) {
        if (!outputBuffer_.empty()) {
            outputByte_ = popFront(outputBuffer_);
            outputDevice_ = 1;
        } else if (!dev2OutputBuffer_.empty()) {
            outputByte_ = popFront(dev2OutputBuffer_);
            outputDevice_ = 2;
        }
        //if (outputDevice_)
        //    LOG("Device {} enqueued {:02X}", outputDevice_, outputByte_);
    }

    if (((ram_[RAM_LOC_CONFIG] & (CONFIG_MASK_PORT1_IRQ | CONFIG_MASK_PORT1_CLOCK_DISABLE)) == CONFIG_MASK_PORT1_IRQ)
        && outputDevice_ == 1)
        onDevice1IRQ_();
    if (((ram_[RAM_LOC_CONFIG] & (CONFIG_MASK_PORT2_IRQ | CONFIG_MASK_PORT2_CLOCK_DISABLE)) == CONFIG_MASK_PORT2_IRQ)
        && outputDevice_ == 2)
        onDevice2IRQ_();
}

void i8042_PS2Controller::impl::setA20State(bool enabled)
{
    LOG("A20 {}", enabled ? "Enable" : "Disable");
    onA20CLinehange_(enabled);
}

std::uint8_t i8042_PS2Controller::impl::inU8(std::uint16_t port, std::uint16_t offset)
{
    switch (offset) {
    case 0: { // Data
        const auto data = outputByte_;
        if (!outputDevice_)
            LOG("Read with empty data buffer! {:02X}", data);
        //else
        //    LOG("Read device {} byte {:02X}", outputDevice_, outputByte_);
        outputDevice_ = 0;
        checkIrq();
        return data;
    }
    case 1: // KB controller port B control register for compatability with 8255
        portB_ ^= 0x10; // Refresh done (used for delay)
        return portB_;
    case 4: // Status
        return status_ | (outputDevice_ ? STATUS_MASK_OUTPUT : 0) | (outputDevice_ == 2 ? STATUS_MASK_PORT2_FULL : 0);
    default:
        LOG("TODO!");
        return IOHandler::inU8(port, offset);
    }
}

void i8042_PS2Controller::impl::outU8(std::uint16_t port, std::uint16_t offset, std::uint8_t value)
{
    //LOG("Port {:02X} value {:02X}, status = {:02X}", port, value, status_);

    switch (offset) {
    case 0: // Data
        if (status_ & STATUS_MASK_COMMAND) {
            switch (nextDest_) {
            case DEST_RAM:
                assert(ramWriteOffset_ != RAM_LOC_NONE);
                LOG("Write to RAM location 0x{:02X} value 0x{:02X}", ramWriteOffset_, value);
                //if (ramWriteOffset_ == RAM_LOC_CONFIG)
                //    value |= CONFIG_MASK_PORT2_CLOCK_DISABLE; // Do not allow port 2 clock to be enabled (if no mouse)
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
                ram_[RAM_LOC_CONFIG] &= ~CONFIG_MASK_PORT2_CLOCK_DISABLE;
                if (expectedCommandBytes_) {
                    if (device1Command_)
                        ERROR("Two commands in progress at same time.");
                    portCommandBytes_[commandPos_++] = value;
                    if (--expectedCommandBytes_ == 0) {
                        device2CommandWithArgs();
                        commandPos_ = 0;
                    }
                    break;
                }
                device2Command(value);
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
            if (!device1Command_)
                ERROR("Two commands in progress at same time.");
            portCommandBytes_[commandPos_++] = value;
            if (--expectedCommandBytes_ == 0) {
                device1CommandWithArgs();
                commandPos_ = 0;
            }
            break;
        }
        device1Command(value);
        checkIrq();
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
            LOG("Test port 2");
            enqueueOutputByte(0x00); // Test passed (<> 0x00 for various failures)
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
            LOG("Test port 1");
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
        case CMD_WRITE_PORT2: // D4
            //LOG("Write next output to port2");
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

void i8042_PS2Controller::impl::enqueueDev2OutputByte(uint8_t data)
{
    dev2OutputBuffer_.push_back(data);
    checkIrq();
}

void i8042_PS2Controller::impl::startCommandWithArgs(uint8_t command, bool device1)
{
    portCommandBytes_[0] = command;
    expectedCommandBytes_ = 1;
    commandPos_ = 1;
    device1Command_ = device1;
}

void i8042_PS2Controller::impl::device1Command(uint8_t command)
{
    enqueueOutputByte(DEV_RSP_ACK);
    switch (command) {
    case 0x05:
        LOG("Keyboard - Ignoring command {:02X}", command);
        break;

    case 0xED: // Set LEDs
    case 0xF3: // Set typematic rate and delay
        LOG("Keyboard - {}", command == 0xED ? "Set LEDs" : "Set typematic rate and delay");
        startCommandWithArgs(command, true);
        break;

    case 0xF2: // Identify keyboard
        LOG("Keyboard - identify");
        break;
    case 0xF4:
        LOG("TODO: Keyboard - enable scanning");
        break;
    case 0xF5:
        LOG("TODO: Keyboard - disable scanning");
        break;
    case 0xFF: // Reset and start self-test
        LOG("Keyboard reset and start self-test");
        enqueueOutputByte(0xAA); // Self-test passed
        break;
    default:
        LOG("Keyboard - Ignoring command {:02X}", command);
        THROW_FLIPFLOP();
    }
}

void i8042_PS2Controller::impl::device1CommandWithArgs()
{
    LOG("TODO: Handle device1 command: {}", HexString(portCommandBytes_, commandPos_));
    enqueueOutputByte(DEV_RSP_ACK);
}

void i8042_PS2Controller::impl::mouseReset()
{
    mouseWrapMode_ = false;
    mouseSampleRate_ = 100; // Sample Rate = 100 samples/sec
    mouseResolution_ = 2; // Resolution = 4 counts/mm
    mouseScaling_ = false; // Scaling = 1:1    
    mouseDataReporting_ = false; //Data Reporting Disabled
    mouseState_ = 0;
    clearMouseData();
}

void i8042_PS2Controller::impl::clearMouseData()
{
    mouseDx_ = 0;
    mouseDy_ = 0;
}

void i8042_PS2Controller::impl::sendMouseData()
{
    if (ram_[RAM_LOC_CONFIG] & CONFIG_MASK_PORT2_CLOCK_DISABLE)
        return;
    if (!mouseDataReporting_)
        return;

    if (!dev2OutputBuffer_.empty()) {
        LOG("Mouse data but output buffer is not empty! (Length = {})", dev2OutputBuffer_.size());
    }

    uint8_t state = mouseState_ | MOUSE_STATE_MASK_ALWAYS1;
    int x = mouseDx_;
    if (x < 0)
        state |= MOUSE_STATE_MASK_XSIGN;
    if (x < -255 || x > 255) {
        state |= MOUSE_STATE_MASK_XOVERFLOW;
        x = x < 0 ? -255 : 255;
    }
    int y = -mouseDy_; // Inverted
    if (y < 0)
        state |= MOUSE_STATE_MASK_YSIGN;
    if (y < -255 || y > 255) {
        state |= MOUSE_STATE_MASK_YOVERFLOW;
        y = y < 0 ? -255 : 255;
    }

    enqueueDev2OutputByte(state);
    enqueueDev2OutputByte(static_cast<uint8_t>(x & 0xff));
    enqueueDev2OutputByte(static_cast<uint8_t>(y & 0xff));
    clearMouseData();
}

void i8042_PS2Controller::impl::device2Command(uint8_t command)
{
    static constexpr uint8_t mouseID = 0; // 0 = Standard PS/2 mouse, 3 = mouse with scroll wheel
    if (mouseWrapMode_ && command != 0xEC && command != 0xFF) {
        LOG("Mouse wrap {:02X}", command);
        enqueueDev2OutputByte(command);
        return;
    }

    enqueueDev2OutputByte(DEV_RSP_ACK);
    switch (command) {
    case 0xE6: // Set Scaling 1:1
        LOG("Mouse - Set Scaling 1:1");
        mouseScaling_ = false;
        break;
    case 0xE7: // Set Scaling 2:1
        LOG("Mouse - Set Scaling 2:1");
        mouseScaling_ = true;
        THROW_ONCE();
        break;
    case 0xE8: // Set Resolution
        LOG("Mouse - Set Resolution");
        startCommandWithArgs(command, false);
        break;
    case 0xE9: // Status Request
        LOG("Mouse - Status Request");
        enqueueDev2OutputByte(mouseDataReporting_ << 5 | mouseScaling_ << 4 | (mouseState_ & 7));
        enqueueDev2OutputByte(mouseResolution_);
        enqueueDev2OutputByte(mouseSampleRate_);
        clearMouseData();
        break;
    case 0xEC: // Reset Wrap Mode 
        LOG("Mouse - Reset Wrap Mode");
        mouseWrapMode_ = false;
        break;
    case 0xEE: // Set Wrap Mode
        LOG("Mouse - Set Wrap Mode");
        mouseWrapMode_ = true;
        break;
    case 0xF2: // Identify keyboard
        LOG("Mouse - identify");
        enqueueDev2OutputByte(mouseID); 
        break;
    case 0xF3: // Set Sample Rate
        LOG("Mouse - Set Sample Rate");
        startCommandWithArgs(command, false);
        break;
    case 0xF4: // Enable Data Reporting
        LOG("Mouse - Enable Data Reporting");
        mouseDataReporting_ = true;
        break;
    case 0xF5: // Disable Data Reporting
        LOG("Mouse - Disable Data Reporting");
        mouseDataReporting_ = false;
        break;
    case 0xFF: // Reset and start self-test
        LOG("Mouse reset and start self-test");
        mouseReset();
        enqueueDev2OutputByte(0xAA); // Self-test passed
        enqueueDev2OutputByte(mouseID); // ID
        break;
    default:
        LOG("Mouse - Ignoring command {:02X}", command);
        THROW_FLIPFLOP();
    }
}

void i8042_PS2Controller::impl::device2CommandWithArgs()
{
    enqueueDev2OutputByte(DEV_RSP_ACK);
    switch (portCommandBytes_[0]) {
    case 0xE8:
        mouseResolution_ = portCommandBytes_[1];
        LOG("Mouse - Set resolution {} counts/mm", 1 << mouseResolution_);
        break;
    case 0xF3:
        mouseSampleRate_ = portCommandBytes_[1];
        LOG("Mouse - Set sample rate {} counts/mm", mouseSampleRate_);
        break;
    default:
        LOG("TODO: Handle device2 command: {}", HexString(portCommandBytes_, commandPos_));
    }
}

void i8042_PS2Controller::impl::mouseMove(int dx, int dy)
{
    mouseDx_ += dx;
    mouseDy_ += dy;
}

void i8042_PS2Controller::impl::mouseButton(int idx, bool down)
{
    assert(idx <= 2);
    const auto mask = static_cast <uint8_t>(1 << idx);
    if (down)
        mouseState_ |= mask;
    else
        mouseState_ &= ~mask;
}

void i8042_PS2Controller::impl::mouseUpdate()
{
    sendMouseData();
}

i8042_PS2Controller::i8042_PS2Controller(SystemBus& bus, CallbackType onDevice1IRQ, CallbackType onDevice2IRQ, A20CallbackType onA20CLinehange)
    : impl_ { std::make_unique<impl>(bus, onDevice1IRQ, onDevice2IRQ, onA20CLinehange) }
{
}

i8042_PS2Controller::~i8042_PS2Controller() = default;

void i8042_PS2Controller::enqueueKey(const KeyPress& key)
{
    impl_->enqueueKey(key);
}

void i8042_PS2Controller::mouseMove(int dx, int dy)
{
    impl_->mouseMove(dx, dy);
}

void i8042_PS2Controller::mouseButton(int idx, bool down)
{
    impl_->mouseButton(idx, down);
}

void i8042_PS2Controller::mouseUpdate()
{
    impl_->mouseUpdate();
}