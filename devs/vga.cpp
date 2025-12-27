#include "vga.h"
#include "debugger.h"
#include <stdexcept>
#include <print>
#include <format>
#include <cstring>

//TODO: See https://www.vogons.org/viewtopic.php?f=9&t=82050&start=60

// Other info:
// https://wiki.osdev.org/VGA_Hardware
// http://www.osdever.net/FreeVGA/vga/vga.htm

#define LOG(...) std::println("VGA: " __VA_ARGS__)
#define ERROR(...) do { std::println( "VGA: " __VA_ARGS__); THROW_FLIPFLOP(); } while (0)

namespace {

constexpr uint32_t Clock16FreqHz = 16257000;

constexpr uint8_t fontReservedHeight = 32;

// Also mirrored at 0x3B4 (for monochrome support)
constexpr uint16_t portCrtcAddress = 0x3D4;
constexpr uint16_t portCrtcData = 0x3D5;
constexpr uint16_t portInputStatus1 = 0x3DA; // Feature Control when written
constexpr uint16_t portFeatureControlWrite = 0x3DA;

constexpr uint16_t portCrtcAddressAlt = 0x3B4;
constexpr uint16_t portCrtcDataAlt = 0x3B5;
constexpr uint16_t portInputStatus1Alt = 0x3BA; // Feature Control when written
constexpr uint16_t portFeatureControlWriteAlt = 0x3BA;

constexpr uint16_t portAttrAddressData = 0x3C0;
constexpr uint16_t portAttrDataRead = 0x3C1;
constexpr uint16_t portAttrInputStatus0 = 0x3C2; // Misc. output when written
constexpr uint16_t portMiscOutWrite = 0x3C2;
// 0x3C3 ?
constexpr uint16_t portSeqAddress = 0x3C4;
constexpr uint16_t portSeqData = 0x3C5;
constexpr uint16_t portPelMask = 0x3C6;
constexpr uint16_t portDacState = 0x3C7; // Address read mode when written
constexpr uint16_t portDacAddress = 0x3c8;
constexpr uint16_t portDacData = 0x3c9;
constexpr uint16_t portFeatureControlRead = 0x3CA; // Read
constexpr uint16_t portGfxPos2 = 0x3CA; // Write
// 0x3CB ?
constexpr uint16_t portMiscOutRead = 0x3CC; // Read
constexpr uint16_t portGfxPos1 = 0x3CC; // Write
// 0x3CD ?
constexpr uint16_t portGfxCtrlAddr = 0x3CE;
constexpr uint16_t portGfxCtrlData = 0x3CF;

constexpr uint8_t INPUT_STATUS_0_MASK_SS = 1 << 4; // Switch sense

constexpr uint8_t INPUT_STATUS_1_MASK_DD = 1 << 0; // When set to 1, this bit indicates a horizontal or vertical retrace interval
constexpr uint8_t INPUT_STATUS_1_MASK_VTRACE = 1 << 3; // When set to 1, this bit indicates that the display is in a vertical retrace interval

constexpr uint8_t ATTR_ADDR_REG_MASK = 0x1f;
constexpr uint8_t ATTR_ADDR_MASK_PAS = 1 << 5; // Palette Address Source -- Must be cleared when loading colors, must be set when palette is in use

constexpr uint8_t MISC_OUT_MASK_IO_SELECT = 1 << 0; // 1 = 3DX / 0 = 3BX (cga/mda emulation)
constexpr uint8_t MISC_OUT_MASK_RAM_ENABLE = 1 << 1;
constexpr uint8_t MISC_OUT_BIT_CLOCK_SOURCE = 2;
constexpr uint8_t MISC_OUT_MASK_CLOCK_SOURCE = 3 << MISC_OUT_BIT_CLOCK_SOURCE;
constexpr uint8_t MISC_OUT_MASK_PAGE_BIT = 1 << 5; // Page bit for odd/even
constexpr uint8_t MISC_OUT_MASK_HSYNCP = 1 << 6; // Horizontal Sync Polarity
constexpr uint8_t MISC_OUT_MASK_VSYNCP = 1 << 7; // Vertical Sync Polarity

enum {
    CLOCK_SOURCE_CPU_14Mhz, // 0b00 -- 14Mhz from processor I/O channel
    CLOCK_SOURCE_INTERAL_16Mhz, // 0b01 -- 16Mhz from on-board oscillator
    CLOCK_SOURCE_EXTERNAL, // 0b10 -- From feature connector
    CLOCK_SOURCE_NOT_USED // 0b11 -- Not used
};

/////////////////////////////////////////////////////////////
// CRT controller
/////////////////////////////////////////////////////////////

enum : uint8_t {
    CRTC_REG_HTOTAL, // 00 -- Horizontal Total Register
    CRTC_REG_HDISPEND, // 01 -- End Horizontal Display Register
    CRTC_REG_HBSTART, // 02 -- Start Horizontal Blanking Register
    CRTC_REG_HBEND, // 03 -- End Horizontal Blanking Register
    CRTC_REG_HRSTART, // 04 -- Start Horizontal Retrace Register
    CRTC_REG_HREND, // 05 -- End Horizontal Retrace Register
    CRTC_REG_VTOTAL, // 06 -- Vertical Total Register
    CRTC_REG_OVERFLOW, // 07 -- Overflow Register
    CRTC_REG_PRESET_ROW_SCAN, // 08 -- Preset Row Scan Register
    CRTC_REG_MAX_SCANLINE, // 09 -- Maximum Scan Line Register
    CRTC_REG_CURSOR_START, // 0A -- Cursor Start Register
    CRTC_REG_CURSOR_END, // 0B -- Cursor End Register
    CRTC_REG_ADDRESS_HIGH, // 0C -- Start Address High Register
    CRTC_REG_ADDRESS_LOW, // 0D -- Start Address Low Register
    CRTC_REG_CURSOR_HIGH, // 0E -- Cursor Location High Register
    CRTC_REG_CURSOR_LOW, // 0F -- Cursor Location Low Register
    CRTC_REG_VRSTART, // 10 -- Start Vertical Retrace Register
    CRTC_REG_VREND, // 11 -- End Vertical Retrace Register
    CRTC_REG_VDEND, // 12 -- Vertical Display End Register
    CRTC_REG_OFFSET, // 13 -- Offset Register
    CRTC_REG_UNDERLINE_LOC, // 14 -- Underline Location Register
    CRTC_REG_VBSTART, // 15 -- Start Vertical Blanking Register
    CRTC_REG_VBEND, // 16 -- End Vertical Blanking Register
    CRTC_REG_MODE_CONTROL, // 17 -- CRTC Mode Control Register
    CRTC_REG_LINE_COMPARE, // 18 -- Line Compare Register
};
static_assert(CRTC_REG_LINE_COMPARE == 0x18);

const char* const crtcRegName[0x19] = {
    "Horizontal Total Register",
    "End Horizontal Display Register",
    "Start Horizontal Blanking Register",
    "End Horizontal Blanking Register",
    "Start Horizontal Retrace Register",
    "End Horizontal Retrace Register",
    "Vertical Total Register",
    "Overflow Register",
    "Preset Row Scan Register",
    "Maximum Scan Line Register",
    "Cursor Start Register",
    "Cursor End Register",
    "Start Address High Register",
    "Start Address Low Register",
    "Cursor Location High Register",
    "Cursor Location Low Register",
    "Start Vertical Retrace Register",
    "End Vertical Retrace Register",
    "Vertical Display End Register",
    "Offset Register",
    "Underline Location Register",
    "Start Vertical Blanking Register",
    "End Vertical Blanking Register",
    "CRTC Mode Control Register",
    "Line Compare Register",
};

// CRTC_REG_OVERFLOW (07)
constexpr uint8_t CRTC_OVERFLOW_VT8 = 1 << 0; // Vertical Total (bit 8)
constexpr uint8_t CRTC_OVERFLOW_VDE8 = 1 << 1; // Vertical Display End (bit 8)
constexpr uint8_t CRTC_OVERFLOW_VRS8 = 1 << 2; // Vertical Retrace Start (bit 8)
constexpr uint8_t CRTC_OVERFLOW_SVB8 = 1 << 3; // Start Vertical Blanking (bit 8)
constexpr uint8_t CRTC_OVERFLOW_LC8 = 1 << 4; // Line Compare (bit 8)
constexpr uint8_t CRTC_OVERFLOW_VT9 = 1 << 5; // Vertical Total (bit 9)
constexpr uint8_t CRTC_OVERFLOW_VDE9 = 1 << 6; // Vertical Display End (bit9)
constexpr uint8_t CRTC_OVERFLOW_VRS9 = 1 << 7; // Vertical Retrace Start (bit 9)

// CRTC_REG_MAX_SCANLINE (09)
constexpr uint8_t CRCT_MAX_SCANLINE_MASK_MAX = 0x1F;
constexpr uint8_t CRCT_MAX_SCANLINE_SVB9 = 1 << 5;
constexpr uint8_t CRCT_MAX_SCANLINE_LC9 = 1 << 6;
constexpr uint8_t CRCT_MAX_SCANLINE_SD = 1 << 7;

// CRTC_REG_MODE_CONTROL (17)
constexpr uint8_t CRTC_MODE_CONTROL_MASK_MAP13 = 1 << 0; // This bit selects the source of bit 13 of the output multiplexer. When this bit is set to 0, bit 0 of the row scan counter is the source, and when this bit is set to 1, bit 13 of the address counter is the source
constexpr uint8_t CRTC_MODE_CONTROL_MASK_MAP14 = 1 << 1; // This bit selects the source of bit 14 of the output multiplexer. When this bit is set to 0, bit 1 of the row scan counter is the source. When this bit is set to 1, the bit 14 of the address counter is the source
constexpr uint8_t CRTC_MODE_CONTROL_MASK_SLDIV = 1 << 2;
constexpr uint8_t CRTC_MODE_CONTROL_MASK_DIV2 = 1 << 3;
constexpr uint8_t CRTC_MODE_CONTROL_MASK_AW = 1 << 5; // Address wrap
constexpr uint8_t CRTC_MODE_CONTROL_MASK_WB = 1 << 6; // Word/byte
constexpr uint8_t CRTC_MODE_CONTROL_MASK_SE = 1 << 7; // Sync enable

/////////////////////////////////////////////////////////////
// Graphics controller
/////////////////////////////////////////////////////////////

enum : uint8_t {
    GC_REG_SET_RESET, // 0
    GC_REG_ENABLE_SET_RESET, // 1
    GC_REG_COLOR_COMPARE, // 2
    GC_REG_DATA_ROTATE, // 3
    GC_REG_READ_MAP_SELECT, // 4
    GC_REG_MODE, // 5
    GC_REG_MISC, // 6
    GC_REG_DONT_CARE, // 7
    GC_REG_BIT_MASK, // 8
};
static_assert(GC_REG_BIT_MASK == 8);

const char* const gcRegName[9] = {
    "Set/Reset Register",
    "Enable Set/Reset Register",
    "Color Compare Register",
    "Data Rotate Register",
    "Read Map Select Register",
    "Graphics Mode Register",
    "Miscellaneous Graphics Register",
    "Color Don't Care Register",
    "Bit Mask Register",
};

// GC_REG_MODE (5)
constexpr uint8_t GC_MODE_MASK_WRITE_MODE = 3 << 0;
constexpr uint8_t GC_MODE_MASK_READ_MODE = 1 << 3;
constexpr uint8_t GC_MODE_MASK_HOST_OE = 1 << 4;
constexpr uint8_t GC_MODE_MASK_SHIFT_REG = 1 << 5;
constexpr uint8_t GC_MODE_MASK_SHIFT256 = 1 << 6;

// GC_REG_MISC (6)
constexpr uint8_t GC_MISC_MASK_ALPHA_DIS = 1 << 0; // Alphanumeric Mode Disable (Graphics mode)
constexpr uint8_t GC_MISC_MASK_CHAIN_OE = 1 << 1; // Chain Odd/Even ("When set to 1, this bit directs the system address bit, A0, to be replaced by a higher-order bit. The odd map is then selected when A0 is 1, and the even map when A0 is 0.")
constexpr uint8_t GC_MISC_BIT_MAP_SEL = 2;
constexpr uint8_t GC_MISC_MASK_MAP_SEL = 3 << GC_MISC_BIT_MAP_SEL;

/////////////////////////////////////////////////////////////
// Sequencer
/////////////////////////////////////////////////////////////

enum : uint8_t {
    SEQ_REG_RESET, // 0
    SEQ_REG_CLOCK_MODE, // 1
    SEQ_REG_MAP_MASK, // 2
    SEQ_REG_CMAP_SELECT, // 3
    SEQ_REG_MEM_MODE, // 4
};
static_assert(SEQ_REG_MEM_MODE == 4);

const char* const seqRegName[5] = {
    "Reset Register",
    "Clocking Mode Register",
    "Map Mask Register",
    "Character Map Select Register",
    "Sequencer Memory Mode Register",
};

// SEQ_REG_RESET (0)
constexpr uint8_t SEQ_RESET_MASK_AR = 1 << 0; // Sychnronous Reset
constexpr uint8_t SEQ_RESET_MASK_SR = 1 << 1; // Asynchronous Reset

// SEQ_REG_CLOCK_MODE (1)
constexpr uint8_t SEQ_CLOCK_MODE_MASK_8DM = 1 << 0; // 8/9 dot mode (0 = 9 dots per char / 1 = 8 dots per char)
constexpr uint8_t SEQ_CLOCK_MODE_MASK_SLR = 1 << 2;
constexpr uint8_t SEQ_CLOCK_MODE_MASK_DCR = 1 << 3;
constexpr uint8_t SEQ_CLOCK_MODE_MASK_S4 = 1 << 4;
constexpr uint8_t SEQ_CLOCK_MODE_MASK_SD = 1 << 5; // Screen disable

// SEQ_REG_MEM_MODE (4)
constexpr uint8_t SEQ_MEM_MODE_MASK_EXT_MEM = 1 << 1; // Eanbles video memory from 64KB .. 256KB
constexpr uint8_t SEQ_MEM_MODE_MASK_OE_DIS = 1 << 2; // When this bit is set to 0, even system addresses access maps 0 and 2, while odd system addresses access maps 1 and 3. When this bit is set to 1, system addresses sequentially access data within a bit map, and the maps are accessed according to the value in the Map Mask register (index 0x02).
constexpr uint8_t SEQ_MEM_MODE_CHAIN4 = 1 << 3;

/////////////////////////////////////////////////////////////
// Attribute controller
/////////////////////////////////////////////////////////////

enum : uint8_t {
    // 00..0F - Palette
    ATTR_REG_MODE_CONTROL = 0x10,
    ATTR_REG_OVERSCAN_COLOR, // 11
    ATTR_REG_PLANE_ENABLE, // 12 -- Setting a bit to 1, enables the corresponding display-memory color plane.
    ATTR_REG_HORIZONTAL_PAN, // 13
    ATTR_REG_COLOR_SELECT, // 14
};
static_assert(ATTR_REG_COLOR_SELECT == 0x14);
static const char* const attrRegName[0x15] = {
    "Palette0",
    "Palette1",
    "Palette2",
    "Palette3",
    "Palette4",
    "Palette5",
    "Palette6",
    "Palette7",
    "Palette8",
    "Palette9",
    "PaletteA",
    "PaletteB",
    "PaletteC",
    "PaletteD",
    "PaletteE",
    "PaletteF",
    "Attribute Mode Control Register",
    "Overscan Color Register",
    "Color Plane Enable Register",
    "Horizontal Pixel Panning Register",
    "Color Select Register",
};

constexpr uint8_t ATTR_MODE_CONTROL_MASK_GRAPHICS = 1 << 0;
constexpr uint8_t ATTR_MODE_CONTROL_MASK_MONOCHROME = 1 << 1;
constexpr uint8_t ATTR_MODE_CONTROL_MASK_LINE_GRAPHICS = 1 << 2;
constexpr uint8_t ATTR_MODE_CONTROL_MASK_BLINKING = 1 << 3;

/////////////////////////////////////////////////////////////

constexpr uint32_t INVALID_OFFSET = UINT32_MAX;

union Pixel {
    uint8_t planes[4];
    uint32_t data;
};
static_assert(sizeof(Pixel) == 4);


template<size_t Size>
const char* RegisterName(const char* const (&names)[Size], uint8_t index)
{
    if (index < Size)
        return names[index];
    return "(Invalid register index)";
}

uint32_t CgaColor(uint8_t value)
{
    const auto i = (value >> 4) & 1 ? 0x55 : 0x00;
    const auto b = i + ((value >> 0) & 1 ? 0xAA : 0x00);
    const auto g = i + ((value >> 1) & 1 ? 0xAA : 0x00);
    const auto r = i + ((value >> 2) & 1 ? 0xAA : 0x00);
    uint32_t color = r << 16 | g << 8 | b;
    if (color == 0xAAAA00)
        return 0xAA5500; // Color "6" "dark yellow" -> brown
    return color;
}

template <size_t Size>
void ShowRegisters(const char* title, const uint8_t (&registers)[Size], const char* const (&names)[Size])
{
    std::println("{} registers:", title);
    for (size_t i = 0; i < Size; ++i)
        std::println("{:02X} = {:02X} 0b{:08b} {}", i, registers[i], registers[i], names[i]);
}


} // unnamed namespace

class VGA::impl : public IOHandler, public CycleObserver, public MemoryHandler {
public:
    explicit impl(SystemBus& bus);

    void reset();
    void setDrawFunction(const DrawFunction& onDraw);

    void runCycles(std::uint64_t numCycles) override;
    std::uint64_t nextAction() override;

    std::uint8_t inU8(std::uint16_t port, std::uint16_t offset) override;
    void outU8(std::uint16_t port, std::uint16_t offset, std::uint8_t value) override;

    std::uint8_t peekU8(std::uint64_t addr, std::uint64_t offset) override;
    std::uint8_t readU8(std::uint64_t addr, std::uint64_t offset) override;
    void writeU8(std::uint64_t addr, std::uint64_t offset, std::uint8_t value) override;

    void renderFrame();
    void registerDebugFunction(Debugger& dbg)
    {
        dbg.registerFunction("vga", [this](DebuggerInterface& dbg, [[maybe_unused]] std::string_view command) {
            this->onDebugCommand(dbg);
        });
    }

private:
    SystemBus& bus_;
    DrawFunction onDraw_;
    std::vector<Pixel> videoMem_;
    std::vector<uint32_t> displayBuffer_;
    const bool ega_ = true;

    uint32_t frameCount_;
    uint64_t frameCycles_;
    Pixel latch_;
    uint32_t palette_[256];
    uint32_t paletteCga_[16];

    struct {
        struct {
            uint16_t total;
            uint16_t displayEnd;
            struct {
                uint16_t start, end;
            } blank, retrace;

            void log(const char* label) const
            {
                LOG("{}total {} displayEnd {}", label, total, displayEnd);
                LOG("{}blank {} {}", label, blank.start, blank.end);
                LOG("{}retrace {} {}", label, retrace.start, retrace.end);
            }
        } h, v;

        void log(bool alphaNumeric, uint8_t planeEnable) const
        {
            LOG("Display:");
            if (!clocksPerLine) {
                LOG("Invalid mode");
                return;
            }

            uint8_t bpp = 0;
            for (int i = 0; i < 4; ++i)
                bpp += (planeEnable >> i) & 1;

            LOG("{}x{}x{}, {} dots/char", dots * (h.displayEnd + 1), v.displayEnd + 1, bpp, dots);
            if (alphaNumeric)
                LOG("Text-mode {}x{} Font size {}x{}", (h.displayEnd + 1), (v.displayEnd + 1) / (charHeight + 1), dots, charHeight + 1);
            h.log("H: ");
            v.log("V: ");

            // clocks/line * (s/clocks)

            LOG("Horizontal frequency: {:.2f} KHz, Display frequency: {:.3f} Hz ({:.2f} fps)", SysClockFreqHz * 0.001 / clocksPerLine, clocksPerFrame() / static_cast<double>(SysClockFreqHz), static_cast<double>(SysClockFreqHz) / clocksPerFrame());
        }

        uint8_t dots, charHeight;
        uint32_t clocksUntilHorizontalBlank;
        uint32_t clocksPerLine;
        uint32_t clocksPerFrame() const
        {
            return clocksPerLine * v.total;
        }
    } displayInfo_, lastMode_;

    bool dataFlipFlop_;

    // Miscellaneous output
    uint8_t miscOut_;

    // Attribute controller
    uint8_t attrAddr_;
    uint8_t attrReg_[0x15];

    // Sequencer
    uint8_t seqAddr_;
    uint8_t seqReg_[0x05];

    // CRT controller
    uint8_t crtcAddr_;
    uint8_t crtcReg_[0x19];

    // Graphics controller
    uint8_t gcAddr_;
    uint8_t gcReg_[0x09];

    // DAC
    uint8_t pelReg_;
    uint8_t pelReadReg_;
    uint8_t pelRegState_;

    bool isSelected(uint16_t port)
    {
        return ((port & 0xf0) == 0xd0) == ((miscOut_ & MISC_OUT_MASK_IO_SELECT) != 0);
    }

    bool displayActive() const;
    void recalcMode();

    uint8_t inputStatus0();
    uint8_t inputStatus1();

    uint32_t mapMem(uint32_t address) const;

    void renderFrameText(const uint32_t* palette);
    void renderFrameGraphics(const uint32_t* palette);

    void onDebugCommand(DebuggerInterface& dbg);
};

VGA::impl::impl(SystemBus& bus)
    : bus_ { bus }
{
    bus.addIOHandler(portCrtcAddress, 2, *this, true);
    bus.addIOHandler(portInputStatus1, 1, *this, true);
    bus.addIOHandler(portCrtcAddressAlt, 2, *this, true);
    bus.addIOHandler(portInputStatus1Alt, 1, *this, true);

    bus.addIOHandler(portAttrAddressData, 16, *this, true);

    bus.addCycleObserver(*this);

    // A0000-BFFFF
    bus.addMemHandler(0xA0000, 128 * 1024, *this, true);

    //videoMem_.resize(16 * 1024); // TODO: Allow more memory (for VGA) and up to 192KB with a daughter board
    videoMem_.resize(64 * 1024);

    reset();
}

void VGA::impl::reset()
{
    frameCount_ = 0;
    frameCycles_ = 0;
    latch_ = Pixel {};
    std::memset(palette_, 0, sizeof(palette_));

    std::memset(&displayInfo_, 0, sizeof(displayInfo_));
    std::memset(&lastMode_, 0, sizeof(lastMode_));

    dataFlipFlop_ = false;

    attrAddr_ = 0;
    std::memset(attrReg_, 0, sizeof(attrReg_));

    seqAddr_ = 0;
    std::memset(seqReg_, 0, sizeof(seqReg_));

    crtcAddr_ = 0;
    std::memset(crtcReg_, 0, sizeof(crtcReg_));

    gcAddr_ = 0;
    std::memset(gcReg_, 0, sizeof(gcReg_));

    miscOut_ = MISC_OUT_MASK_IO_SELECT;

    seqReg_[SEQ_REG_CLOCK_MODE] = SEQ_CLOCK_MODE_MASK_SD;

    pelReg_ = 0;
    pelReadReg_ = 0;
    pelRegState_ = 0;
}

void VGA::impl::setDrawFunction(const DrawFunction& onDraw)
{
    onDraw_ = onDraw;
}

bool VGA::impl::displayActive() const
{
    if (!(crtcReg_[CRTC_REG_MODE_CONTROL] & CRTC_MODE_CONTROL_MASK_SE))
        return false;

    if (seqReg_[SEQ_REG_MEM_MODE] & SEQ_CLOCK_MODE_MASK_SD)
        return false;

    if ((seqReg_[SEQ_REG_RESET] & (SEQ_RESET_MASK_AR | SEQ_RESET_MASK_SR)) != (SEQ_RESET_MASK_AR | SEQ_RESET_MASK_SR))
        return false;

    return true;
}

void VGA::impl::recalcMode()
{
    if (!displayActive())
        return;

    // 0 = 14.31818MHz processor clock, 1 = 16Mhz on-board oscillator
    const auto clockSource = (miscOut_ & MISC_OUT_MASK_CLOCK_SOURCE) >> MISC_OUT_BIT_CLOCK_SOURCE;
    if (clockSource > CLOCK_SOURCE_INTERAL_16Mhz)
        ERROR("Invalid clock source 0b{:02b}", clockSource);

    displayInfo_.dots = seqReg_[SEQ_REG_CLOCK_MODE] & SEQ_CLOCK_MODE_MASK_8DM ? 8 : 9;
    displayInfo_.charHeight = crtcReg_[CRTC_REG_MAX_SCANLINE] & CRCT_MAX_SCANLINE_MASK_MAX;

    auto& h = displayInfo_.h;
    auto& v = displayInfo_.v;

    h.total = crtcReg_[CRTC_REG_HTOTAL] + 2; // Horizontal total (characters - 1) ... actually -2 for EGA and -5 for VGA
    h.displayEnd = crtcReg_[CRTC_REG_HDISPEND]; // End Horizontal Display Register
    h.blank.start = crtcReg_[CRTC_REG_HBSTART]; // Start horizontal blanking
    h.blank.end = h.blank.start + ((crtcReg_[CRTC_REG_HBEND] & 0x1f) | (crtcReg_[CRTC_REG_HREND] >> 7) << 5); // End horizontal blanking
    h.retrace.start = crtcReg_[CRTC_REG_HRSTART];
    h.retrace.end = h.retrace.start + (crtcReg_[CRTC_REG_HREND] & 0x1f);

#define OFL(name) ((crtcReg_[CRTC_REG_OVERFLOW] & CRTC_OVERFLOW_##name) != 0)
    v.total = crtcReg_[CRTC_REG_VTOTAL] | OFL(VT8) << 8 | OFL(VT9) << 9;
    v.displayEnd = crtcReg_[CRTC_REG_VDEND] | OFL(VDE8) << 8 | OFL(VDE9) << 9;
    v.blank.start = crtcReg_[CRTC_REG_VBSTART] | OFL(SVB8) << 8 | ((crtcReg_[CRTC_REG_MAX_SCANLINE] & CRCT_MAX_SCANLINE_SVB9) != 0) << 9;
    v.blank.end = v.blank.start + (crtcReg_[CRTC_REG_VBEND] & 0x1f);
    v.retrace.start = crtcReg_[CRTC_REG_VRSTART] | OFL(VRS8) << 8 | OFL(VRS9) << 9;
    v.retrace.end = v.retrace.start + (crtcReg_[CRTC_REG_VREND] & 0xf);
#undef OFL

    displayInfo_.clocksPerLine = h.total * displayInfo_.dots;
    displayInfo_.clocksUntilHorizontalBlank = (h.displayEnd + 1) * displayInfo_.dots;

    // Dot clock rate
    if (seqReg_[SEQ_REG_CLOCK_MODE] & SEQ_CLOCK_MODE_MASK_DCR) {
        displayInfo_.clocksPerLine <<= 1;
        displayInfo_.clocksUntilHorizontalBlank <<= 1;
    }

    if (clockSource == CLOCK_SOURCE_INTERAL_16Mhz) {
        const double adjust = SysClockFreqHz / static_cast<double>(Clock16FreqHz);
        displayInfo_.clocksPerLine = static_cast<uint32_t>(displayInfo_.clocksPerLine * adjust);
        displayInfo_.clocksUntilHorizontalBlank = static_cast<uint32_t>(displayInfo_.clocksUntilHorizontalBlank * adjust);
    }

    frameCycles_ = 0;
    bus_.recalcNextAction();
}

void VGA::impl::runCycles(std::uint64_t numCycles)
{
    if (!displayActive()) {
        frameCycles_ = 0;
        return;
    }
    frameCycles_ += numCycles;
    while (frameCycles_ >= displayInfo_.clocksPerFrame()) {
        frameCycles_ -= displayInfo_.clocksPerFrame();
        renderFrame();
        ++frameCount_;
    }
}

std::uint64_t VGA::impl::nextAction()
{
    if (!displayActive() || !displayInfo_.clocksPerLine)
        return UINT64_MAX;

    assert(frameCycles_ < displayInfo_.clocksPerFrame());
    return displayInfo_.clocksPerFrame() - frameCycles_;
}

void VGA::impl::renderFrame()
{
    if (std::memcmp(&displayInfo_, &lastMode_, sizeof(displayInfo_))) {
        LOG("Mode switch!");
        std::memcpy(&lastMode_, &displayInfo_, sizeof(displayInfo_));
        // for (size_t i = 0; i < std::size(crtcReg_); ++i)
        //     std::println("CRTC reg {:02X} = {:02X} {}", i, crtcReg_[i], crtcRegName[i]);
        displayInfo_.log(!(gcReg_[GC_REG_MISC] & GC_MISC_MASK_ALPHA_DIS), attrReg_[ATTR_REG_PLANE_ENABLE]);
        displayBuffer_.resize((displayInfo_.v.displayEnd + 1) * (displayInfo_.h.displayEnd + 1) * displayInfo_.dots);
    }

    if (!displayActive() || !displayInfo_.clocksPerLine) {
        onDraw_(nullptr, 0, 0);
        return;
    }

    // Use CGA palette when 14MHz clock is used
    const uint32_t* palette = palette_;
    if (((miscOut_ & MISC_OUT_MASK_CLOCK_SOURCE) >> MISC_OUT_BIT_CLOCK_SOURCE) == CLOCK_SOURCE_CPU_14Mhz)
        palette = paletteCga_;

    if (gcReg_[GC_REG_MISC] & GC_MISC_MASK_ALPHA_DIS)
        renderFrameGraphics(palette);
    else
        renderFrameText(palette);

    onDraw_(displayBuffer_.data(), (displayInfo_.h.displayEnd + 1) * displayInfo_.dots, displayInfo_.v.displayEnd + 1);
}

void VGA::impl::renderFrameGraphics(const uint32_t* palette)
{
    if (!(attrReg_[ATTR_REG_MODE_CONTROL] & ATTR_MODE_CONTROL_MASK_GRAPHICS))
        ERROR("TODO: Attribute mode control in graphics mode: 0b{:04b}", attrReg_[ATTR_REG_MODE_CONTROL]);

    const auto modeControl = crtcReg_[CRTC_REG_MODE_CONTROL];

    if (displayInfo_.dots != 8)
        ERROR("TODO: Graphics mode with dots={}", displayInfo_.dots);

    const uint16_t startAddress = crtcReg_[CRTC_REG_ADDRESS_HIGH] << 8 | crtcReg_[CRTC_REG_ADDRESS_LOW];
    const uint16_t addressMask = static_cast<uint16_t>(videoMem_.size() - 1);
    const int numChars = (displayInfo_.h.displayEnd + 1);
    const int screenWidth = numChars * displayInfo_.dots;
    const int screenHeight = displayInfo_.v.displayEnd + 1;
    const bool wordMode = !(modeControl & CRTC_MODE_CONTROL_MASK_WB);
    const uint16_t rowDelta = crtcReg_[CRTC_REG_OFFSET] * 2;
    const auto colorPlaneEnable = attrReg_[ATTR_REG_PLANE_ENABLE];
    const auto shiftInterleaveMode = !!(gcReg_[GC_REG_MODE] & GC_MODE_MASK_SHIFT_REG);

    for (int y = 0, row = 0, rowScanCounter = 0; y < screenHeight; ++y) {
        const uint16_t rowStartAddress = static_cast<uint16_t>(startAddress + row * rowDelta);
        for (int ch = 0; ch < numChars; ++ch) {
            uint16_t ma = static_cast<uint16_t>(rowStartAddress + ch);
            if (wordMode)
                ma = (ma << 1) | ((ma >> (modeControl & CRTC_MODE_CONTROL_MASK_AW ? 15 : 13)) & 1);
            if (!(modeControl & CRTC_MODE_CONTROL_MASK_MAP13))
                ma = (ma & ~(1 << 13)) | (rowScanCounter & 1) << 13;
            if (!(modeControl & CRTC_MODE_CONTROL_MASK_MAP14))
                ma = (ma & ~(1 << 14)) | (rowScanCounter & 2) << 13;

            auto pix = videoMem_[ma & addressMask];
            for (int sx = 0; sx < 8; ++sx) {
                uint8_t pixelVal = 0;
                if (shiftInterleaveMode) {
                    const int shift = 2 * (3 - (sx & 3));
                    pixelVal |= (pix.planes[(sx >> 2)] >> shift) & 3;
                    pixelVal |= ((pix.planes[(sx >> 2) + 2] >> shift) & 3) << 2;
                } else {
                    const int shift = 7 - sx;
                    pixelVal |= ((pix.planes[0] >> shift) & 1);
                    pixelVal |= ((pix.planes[1] >> shift) & 1) << 1;
                    pixelVal |= ((pix.planes[2] >> shift) & 1) << 2;
                    pixelVal |= ((pix.planes[3] >> shift) & 1) << 3;
                }
                displayBuffer_[ch * displayInfo_.dots + sx + y * screenWidth] = palette[pixelVal & colorPlaneEnable];
            }
        }

        if (rowScanCounter++ == displayInfo_.charHeight) {
            ++row;
            rowScanCounter = 0;
        }
    }

}

void VGA::impl::renderFrameText(const uint32_t* palette)
{
    if (auto modeControl = crtcReg_[CRTC_REG_MODE_CONTROL]; (modeControl & ~CRTC_MODE_CONTROL_MASK_WB) != 0xA3)
        ERROR("TODO: Text mode with CRTC Mode Control 0b{:08b} 0x{:02X}", modeControl, modeControl);

    // TODO: Unify text/graphics modes rendering
    // TODO: Handle font selection
    if (auto charSetControl = seqReg_[SEQ_REG_CMAP_SELECT] & 0x3f; charSetControl)
        ERROR("TODO: Text mode with charSetControl={:08b}", charSetControl);

    const uint32_t startAddress = crtcReg_[CRTC_REG_ADDRESS_HIGH] << 8 | crtcReg_[CRTC_REG_ADDRESS_LOW];
    const int numColumns = displayInfo_.h.displayEnd + 1;
    const uint32_t rowOffsetDelta = crtcReg_[CRTC_REG_OFFSET] * 2;

    const uint32_t charAddrMask = static_cast<uint32_t>(videoMem_.size() - 1);

    const int fontHeight = displayInfo_.charHeight + 1;
    const int screenWidth = numColumns * displayInfo_.dots;
    const int screenHeight = displayInfo_.v.displayEnd + 1;

    const auto attrModeControl = attrReg_[ATTR_REG_MODE_CONTROL];
    if (attrModeControl & (ATTR_MODE_CONTROL_MASK_LINE_GRAPHICS | ATTR_MODE_CONTROL_MASK_GRAPHICS)) {
        static bool warned;
        if (!warned) {
            warned = true;
            std::println("TODO: Attribute mode control in alpha numberic mode: 0b{:04b}", attrModeControl);
            THROW_ONCE();
        }
    }

    const bool blinkState = attrModeControl & ATTR_MODE_CONTROL_MASK_BLINKING ? (frameCount_ >> 3) & 1 : true; // On/Off for 8 frames
    const uint8_t bgColorMask = attrModeControl & ATTR_MODE_CONTROL_MASK_BLINKING ? 0x07 : 0x0f;

    const auto modeControl = crtcReg_[CRTC_REG_MODE_CONTROL];

    uint32_t charAddr = startAddress;
    for (int y = 0; y < screenHeight; y += fontHeight, charAddr += rowOffsetDelta) {
        for (int column = 0; column < numColumns; ++column) {
            uint16_t ma = static_cast<uint16_t>(charAddr + column);
            if (!(modeControl & CRTC_MODE_CONTROL_MASK_WB)) // Word mode
                ma = (ma << 1) | ((ma >> (modeControl & CRTC_MODE_CONTROL_MASK_AW ? 15 : 13)) & 1);
            const auto charAttr = videoMem_[ma & charAddrMask];
            const auto bgColor = palette[(charAttr.planes[1] >> 4) & bgColorMask];
            const auto fgColor = !(charAttr.planes[1] & 0x80) || blinkState ? palette[charAttr.planes[1] & 0xf] : bgColor;
            const Pixel* fontData = &videoMem_[charAttr.planes[0] * fontReservedHeight];
            for (int cy = 0; cy < fontHeight; ++cy) {
                uint8_t font = fontData[cy].planes[2];
                for (int cx = 0; cx < displayInfo_.dots; ++cx, font <<= 1) {
                    displayBuffer_[(cx + column * displayInfo_.dots) + (y + cy) * screenWidth] = font & 0x80 ? fgColor : bgColor;
                }
            }
        }
    }

    constexpr uint8_t CRTC_CURSOR_START_CD = 1 << 5; // Cursor disable
    if (((frameCount_ >> 4) & 1) && !(crtcReg_[CRTC_REG_CURSOR_START] & CRTC_CURSOR_START_CD)) { // Every 16th frame

        const uint32_t cursorAddress = (crtcReg_[CRTC_REG_CURSOR_HIGH] << 8 | crtcReg_[CRTC_REG_CURSOR_LOW]) - startAddress;
        const int cursorX = cursorAddress % numColumns;
        const int cursorY = cursorAddress / numColumns;
        const int cursorStart = crtcReg_[CRTC_REG_CURSOR_START] & 0x1f;
        int cursorEnd = crtcReg_[CRTC_REG_CURSOR_END] & 0x1f;
        if (!cursorEnd)
            cursorEnd = fontHeight; // 0 seems to mean end (EGA BIOS CALC_CURSOR)

        if (cursorX <= displayInfo_.h.displayEnd && cursorY <= displayInfo_.v.displayEnd) {
            const auto color = palette[videoMem_[(startAddress + cursorX + cursorY * rowOffsetDelta) & charAddrMask].planes[1] & 15];
            for (int y = cursorStart; y < cursorEnd && y < fontHeight; ++y) {
                for (int x = 0; x < displayInfo_.dots; ++x)
                    displayBuffer_[x + cursorX * displayInfo_.dots + (y + cursorY * fontHeight) * screenWidth] = color;
            }
        }
    }
}

uint8_t VGA::impl::inputStatus0()
{
    // Switch sense is determined by the clock selection in misc. out
    // Logical 0 = switch closed, https://minuszerodegrees.net/ibm_ega/ibm_ega_switch_settings.htm
    //const uint8_t switchSetting = 0b0001; // IBM 5153 (CGA) monitor connected
    const uint8_t switchSetting = 0b1001; // IBM 5154 (EGA) monitor connected
    const auto switchNum = static_cast<uint8_t>(((miscOut_ & MISC_OUT_MASK_CLOCK_SOURCE) >> MISC_OUT_BIT_CLOCK_SOURCE));
    uint8_t val = (switchSetting >> switchNum) & 1 ? INPUT_STATUS_0_MASK_SS : 0;
    if (ega_)
        val |= 0xf;
    LOG("TODO: InputStatus #0 -> {:02X}", val);
    return val;
}

uint8_t VGA::impl::inputStatus1()
{
    dataFlipFlop_ = false; // Reading the input status register clears the address/data flip flop

    uint8_t ret = 0;

    if (!displayActive() || !displayInfo_.clocksPerLine) {
        ret |= INPUT_STATUS_1_MASK_VTRACE | INPUT_STATUS_1_MASK_DD;
    } else {
        const auto vpos = frameCycles_ / displayInfo_.clocksPerLine;
        const auto hpos = (frameCycles_ % displayInfo_.clocksPerLine) / displayInfo_.dots;

        if (vpos > displayInfo_.v.displayEnd)
            ret |= INPUT_STATUS_1_MASK_VTRACE | INPUT_STATUS_1_MASK_DD;
        else if (hpos > displayInfo_.h.displayEnd)
            ret |= INPUT_STATUS_1_MASK_DD;

        // Diagnostics bits:
        // These bits are selectively  connected to two of the six color outputs of the
        // Attribute Controller. The Color Plane Enable  register controls the multiplexer for the video
        // wiring. The following table illustrates the  combinations available and the color output wiring.
        // Color Plane | Input Status 1
        // Bit 5 Bit 4 | Bit 5 / Bit 4
        // -----------------------------
        //     0     0 | Red / Blue
        //     0     1 | 2nd Blue / Green
        //     1     0 | 2nd Red / 2nd Green
        //     1     1 | Not used / Not used

        // Just fake enough top pass EGA BIOS POD14_10 test.

        if (!(ret & INPUT_STATUS_1_MASK_DD))
            ret |= 1 << 4 | 1 << 5;
    }

    return ret;
}

std::uint8_t VGA::impl::inU8(std::uint16_t port, [[maybe_unused]] std::uint16_t offset)
{
    switch (port) {
    case portAttrAddressData:
        LOG("Warning: Read from portAttrAddressData ({:04X})", portAttrAddressData);
        return attrAddr_;
    case portAttrDataRead: // 0x3C1
    {
        const auto reg = static_cast<uint8_t>(attrAddr_ & ATTR_ADDR_REG_MASK);
        if (reg >= std::size(attrReg_)) {
            LOG("Read from invalid attribute controller register {:02X}", reg);
            return 0xFF;
        }
        return attrReg_[reg];
    }        
    case portAttrInputStatus0: // 0x3C2
        return inputStatus0();
    case portSeqData: // 0x3C5
        if (seqAddr_ >= std::size(seqReg_)) {
            LOG("Read from invalid sequencer register {:02X}", seqAddr_);
            return 0xFF;
        }
        return seqReg_[seqAddr_];
    case portDacData: // 0x3C9
        LOG("TODO: read from port {:04X} portDacData", portDacData);
        return 0xFF;
    case portMiscOutRead: // 0x3CC
        return miscOut_;
    case portGfxCtrlData: // 0x3CF
        if (gcAddr_ >= std::size(gcReg_)) {
            LOG("Read from invalid graphics controller register {:02X}", gcAddr_);
            return 0xFF;
        }
        return gcReg_[gcAddr_];

    case portCrtcDataAlt:// 0x3B5
        if (!isSelected(port)) {
            LOG("Read from register {:03X} when not selected", offset);
            return 0xff;
        }
        break;
    case portCrtcData: // 0x3D5
        if (crtcAddr_ >= std::size(crtcReg_))
            ERROR("Read from invalid CRT controller register {:02X}", crtcAddr_);
        return crtcReg_[crtcAddr_];

    case portInputStatus1: // 0x3DA
    case portInputStatus1Alt: // 0x3BA
        if (!isSelected(port)) {
            LOG("Read from register {:03X} when not selected", offset);
            return 0xff;
        }
        return inputStatus1();
    }
    throw std::runtime_error { std::format("TODO: VGA in8 from port {:03X}", port) };
}

void VGA::impl::outU8(std::uint16_t port, [[maybe_unused]] std::uint16_t offset, std::uint8_t value)
{
    switch (port) {
        //https://www.vogons.org/viewtopic.php?f=9&t=82050&start=60
        //EGA BIOS (POD14_10) writes to 0x3C1 instead of 0x3C0!
    case portAttrAddressData + 1: // 0x3C1
        assert(ega_);
        [[fallthrough]];
    case portAttrAddressData: // 0x3C0
        if (!dataFlipFlop_) {
            attrAddr_ = value;
        } else {
            const auto reg = static_cast<uint8_t>(attrAddr_ & ATTR_ADDR_REG_MASK);
            if (reg >= std::size(attrReg_))
                ERROR("Write to invalid attribute controller register {:02X} value {:02X}", reg, value);

            if (reg < 0x10) {
                // EGA palette register
                uint32_t color = 0;
                for (int i = 0; i < 3; ++i) {
                    const int intensity = ((value >> i) & 1) << 1 | ((value >> (3 + i)) & 1);
                    color |= (intensity * 0x55) << (8 * i);
                }
                paletteCga_[reg] = CgaColor(value);

                //LOG("Palette 0x{:02X}: {:06b} -> {:06X} {} {} {} CGA: {:06X}", reg, value, color, inten[0], inten[1], inten[2], paletteCga_[reg]);
                palette_[reg] = color;
            } else {
                LOG("TODO: Attribute controller register {:02X} value {:02X} 0b{:08b} ({})", reg, value, value, RegisterName(attrRegName, reg));
            }

            attrReg_[reg] = value;
        }
        dataFlipFlop_ = !dataFlipFlop_;
        break;
    case portMiscOutWrite: // 0x3C2
        LOG("Misc. out {:02X} {:08b}", value, miscOut_);
        miscOut_ = value;
        break;
    case portSeqAddress: // 0x3C4
        seqAddr_ = value & 0x1f;
        break;
    case portSeqData: // 0x3C5
        if (seqAddr_ != SEQ_REG_MAP_MASK)
            LOG("TODO: Sequencer register {:02X} value {:02X} 0b{:08b} ({})", seqAddr_, value, value, RegisterName(seqRegName, seqAddr_));
        if (seqAddr_ >= std::size(seqReg_)) {
            if (seqAddr_ == 5) {
                // IBM EGA BIOS!
                LOG("Write to invalid sequencer register {:02X} value {:02X}", seqAddr_, value);
                break;
            }
            ERROR("Write to invalid sequencer register {:02X} value {:02X}", seqAddr_, value);
        }
        seqReg_[seqAddr_] = value;
        break;
    case portPelMask:
        LOG("TODO: Write to PEL mask register {:02X}", value);
        if (value != 0xFF)
            ERROR("Unsupported PEL mask {:02X}", value);
        break;
    case portCrtcAddress: // 0x3D4
    case portCrtcAddressAlt: // 0x3B4
        if (!isSelected(port)) {
            LOG("Write to register {:03X} when not selected value {:02X}", port, value);
            return;
        }
        crtcAddr_ = value & 0x1f;
        break;
    case portCrtcData: // 0x3D5
    case portCrtcDataAlt: // 0x3B5
        if (!isSelected(port)) {
            LOG("Write to register {:03X} when not selected value {:02X}", port, value);
            return;
        }
        if (crtcAddr_ != CRTC_REG_CURSOR_HIGH && crtcAddr_ != CRTC_REG_CURSOR_LOW)
            LOG("TODO: CRTC register {:02X} value {:02X} 0b{:08b} ({})", crtcAddr_, value, value, RegisterName(crtcRegName, crtcAddr_));
        if (crtcAddr_ >= std::size(crtcReg_))
            ERROR("Write to invalid CRT controller register {:02X} value {:02X}", crtcAddr_, value);
        if (crtcAddr_ == CRTC_REG_VREND && (value & 0x80))
            ERROR("TODO: Protect bit set in VREND");
        crtcReg_[crtcAddr_] = value;
        break;
    case portDacState:
        pelReadReg_ = value;
        pelRegState_ = 0; // ? Maybe this disjont for reads and writes
        break;
    case portDacAddress: // 0x3c8
        pelReg_ = value;
        pelRegState_ = 0;
        break;
    case portDacData: // 0x3c9
        LOG("TODO: Write to PEL DATA {:02X}:{} {:02X}", pelReg_, pelRegState_, value);
        ++pelRegState_;
        if (pelRegState_ == 3)
            pelRegState_ = 0;
        break;

    case portGfxPos2: // 0x3CA:
        LOG("Graphics position 2: {:02X}", value);
        if (value != 1)
            LOG("Warning: Graphics position 2: {:02X}, should be 1", value);
        break;
    case portGfxPos1: // 0x3CC:
        LOG("Graphics position 1: {:02X}", value);
        if (value != 0)
            LOG("Warning: Graphics position 1: {:02X}, should be 0", value);
        break;
    case portGfxCtrlAddr: // 0x3CE
        gcAddr_ = value & 0xf;
        break;
    case portGfxCtrlData: // 0x3CF
        if (gcAddr_ != GC_REG_DATA_ROTATE && gcAddr_ != GC_REG_BIT_MASK)
            LOG("TODO: Graphics controller register {:02X} value {:02X} 0b{:08b} ({})", gcAddr_, value, value, RegisterName(gcRegName, gcAddr_));
        if (gcAddr_ >= std::size(gcReg_))
            ERROR("Write to invalid graphics controller register {:02X} value {:02X}", gcAddr_, value);
        gcReg_[gcAddr_] = value;
        break;
    case portFeatureControlWrite: // 0x3DA
    case portFeatureControlWriteAlt: // 0x3BA
        if (!isSelected(port)) {
            LOG("Write to register {:03X} when not selected value {:02X}", offset, value);
            return;
        }
        LOG("TODO: Feature control write: {:02X}", value);
        break;
    default:
        throw std::runtime_error { std::format("TODO: VGA out8 to port {:03X} value {:02X}", port, value) };
    }

    // TODO: Not necessary for e.g. cursor position change
    recalcMode();
}

uint32_t VGA::impl::mapMem(uint32_t address) const
{
    if (!(miscOut_ & MISC_OUT_MASK_RAM_ENABLE))
        return INVALID_OFFSET;

//TODO:
//The word mode or byte mode is selected by the Word/Byte (W/B) field in the 
//Mode Control Register. Host address bit A14 or A16 is selected for use as 
//EGNVGA address bit MAOO by the Address Wrap (A W) field in the Mode Con
//trol Register.

    uint32_t base, size;
    switch ((gcReg_[GC_REG_MISC] & GC_MISC_MASK_MAP_SEL) >> GC_MISC_BIT_MAP_SEL) {
    default:
    case 0b00: // A0000h-BFFFFh (128K region)
        base = 0xA0000;
        size = 128 * 1024;
        break;
    case 0b01: // A0000h-AFFFFh (64K region)
        base = 0xA0000;
        size = 64 * 1024;
        break;
    case 0b10: // B0000h-B7FFFh (32K region)
        base = 0xB0000;
        size = 32 * 1024;
        break;
    case 0b11: // B8000h-BFFFFh (32K region)
        base = 0xB8000;
        size = 32 * 1024;
        break;
    }
    if (address < base || address >= base + size) {
        //LOG("Warning graphics address {:05X} is not currently mapped! ({:05X} {:05X})", address, base, size);
        return INVALID_OFFSET;
    }

    address -= base;

    #if 0
    const auto modeControl = crtcReg_[CRTC_REG_MODE_CONTROL];
    if (!(modeControl & CRTC_MODE_CONTROL_MASK_WB)) {
        address <<= 1;
        address |= (address >> (modeControl & CRTC_MODE_CONTROL_MASK_AW ? 16 : 14)) & 1;
    }
    #else
    if (!(seqReg_[SEQ_REG_MEM_MODE] & SEQ_MEM_MODE_MASK_OE_DIS)) {
        address &= ~1; // TODO: Bit is replaced with "higher order bit"
    }
    #endif

    return address & (videoMem_.size() - 1);
}

std::uint8_t VGA::impl::peekU8(std::uint64_t addr, std::uint64_t offset)
{
    const auto savedLatch = latch_;
    const auto res = readU8(addr, offset);
    latch_ = savedLatch;
    return res;
}

std::uint8_t VGA::impl::readU8(std::uint64_t addr, std::uint64_t)
{
    const auto offset = mapMem(static_cast<uint32_t>(addr));
    if (offset == INVALID_OFFSET) {
        //std::println("Read from invalid address {:06X}", addr);
        return 0xFF;
    }
    assert(offset < videoMem_.size());

    if (const auto readMode = gcReg_[GC_REG_MODE] & GC_MODE_MASK_READ_MODE; readMode)
        ERROR("TODO: readMode {} not supported", readMode);

    uint8_t plane = gcReg_[GC_REG_READ_MAP_SELECT];
    // XXX: How does this work?
    if (!(seqReg_[SEQ_REG_MEM_MODE] & SEQ_MEM_MODE_MASK_OE_DIS) && (addr & 1))
        ++plane;

    latch_ = videoMem_[offset];
    return latch_.planes[plane & 3];
}

void VGA::impl::writeU8(std::uint64_t addr, std::uint64_t, std::uint8_t value)
{
    const auto offset = mapMem(static_cast<uint32_t>(addr));
    if (offset == INVALID_OFFSET) {
        //std::println("Write toinvalid address {:06X} value {:02X}", addr, value);
        return;
    }
    assert(offset < videoMem_.size());

    const auto writeMode = gcReg_[GC_REG_MODE] & GC_MODE_MASK_WRITE_MODE;
    const auto logicOp = (gcReg_[GC_REG_DATA_ROTATE] >> 3) & 3;
    const auto enableSetReset = gcReg_[GC_REG_ENABLE_SET_RESET] & 0xf;
    const auto setReset = gcReg_[GC_REG_SET_RESET] & 0xf;
    const auto bitMask = gcReg_[GC_REG_BIT_MASK];

    //
    // 1. The input byte is rotated right by the amount specified in Rotate Count
    //
    if (writeMode == 0 || writeMode == 3) {
        if (const auto rotateCount = gcReg_[GC_REG_DATA_ROTATE] & 7; rotateCount)
            value = value >> rotateCount | value << (8 - rotateCount);
        if (writeMode == 3)
            value &= bitMask;
    }

    //
    // 2. The resulting byte is distributed over 4 separate paths, one for each plane of memory
    //
    Pixel pipelinePixel;

    if (writeMode == 1) {
        // In this mode, data is transferred directly from the 32 bit latch register to display memory, affected only by the Memory Plane Write Enable field.
        // The host data is not used in this mode.
        pipelinePixel = latch_;
    } else {
        for (int plane = 0; plane < 4; ++plane) {
            //
            // 3. If a bit in the Enable Set/Reset register is clear, the corresponding byte is left unmodified.
            //    Otherwise the byte is replaced by all 0s if the corresponding bit in Set/Reset Value is clear,
            //    or all 1s if the bit is one.
            //
            const auto planeMask = 1 << plane;

            uint8_t input = value, aluResult = 0;
            if (writeMode == 0) {
                input = enableSetReset & planeMask ? (setReset & planeMask ? 0xFF : 0x00) : value;
            } else if (writeMode == 2) {
                // The input value is the host data replicated to all four bits
                input = value & planeMask ? 0xFF : 0x00;
            } else if (writeMode == 3) {
                input = setReset & planeMask ? 0xFF : 0x00;
                aluResult = (input & value) | (latch_.planes[plane] & ~value);
            }

            //
            // 4. The resulting value and the latch value are passed to the ALU
            //    Depending of the value of Logical Operation, the following operation is performed:
            //
            if (writeMode != 3) {
                aluResult = latch_.planes[plane];
                switch (logicOp) {
                default:
                case 0: // The byte from the set/reset operation is forwarded
                    aluResult = input;
                    break;
                case 1: // Both inputs are ANDed together
                    aluResult &= input;
                    break;
                case 2: // Both inputs are ORed together
                    aluResult |= input;
                    break;
                case 3: // Both inputs are XORed together
                    aluResult ^= input;
                    break;
                }
                // 5. The Bit Mask Register is checked, for each set bit the corresponding bit from the ALU is forwarded.
                //    If the bit is clear the bit is taken directly from the Latch.
                aluResult = (aluResult & bitMask) | (latch_.planes[plane] & ~bitMask);
            }

            pipelinePixel.planes[plane] = aluResult;
        }
    }

    auto planeWriteEnable = seqReg_[SEQ_REG_MAP_MASK] & 0xf;
    
    if (!(seqReg_[SEQ_REG_MEM_MODE] & SEQ_MEM_MODE_MASK_OE_DIS))
        planeWriteEnable &= 0b0101 << (addr & 1);

    auto& pixel = videoMem_[offset];
    for (int plane = 0; plane < 4; ++plane) {
        if (!(planeWriteEnable & (1 << plane)))
            continue;
        pixel.planes[plane] = pipelinePixel.planes[plane];
    }
}

void VGA::impl::onDebugCommand(DebuggerInterface& dbg)
{
    constexpr int FLAG_GC = 1 << 0;
    constexpr int FLAG_SEQ = 1 << 1;
    constexpr int FLAG_ATTR = 1 << 2;
    constexpr int FLAG_CRTC = 1 << 3;
    constexpr int FLAG_EXT = 1 << 4;
    constexpr int FLAG_MODE = 1 << 5;
    int showFlag = FLAG_GC | FLAG_SEQ | FLAG_ATTR | FLAG_CRTC | FLAG_EXT | FLAG_MODE;
    if (auto w = dbg.getString(); w) {
        if (*w == "mem") {
            auto addr = dbg.getNumber();
            if (!addr)
                throw std::runtime_error { "Usage: vga mem address" };
            if (*addr >= videoMem_.size())
                throw std::runtime_error { std::format("Address out of range (0x{:X})", videoMem_.size()) };

            for (size_t i = *addr, end = std::min(i + 16, videoMem_.size()); i < end; ++i) {
                const auto& pix = videoMem_[i];
                char c = '.';
                if (pix.planes[0] >= ' ' && pix.planes[0] < 0x7f)
                    c = pix.planes[0];
                std::println("{:04X} {:02X} {:02X} {:02X} {:02X}  {:c}", i, pix.planes[0], pix.planes[1], pix.planes[2], pix.planes[3], c);
            }

            return;
        }

        if (*w == "gc")
            showFlag = FLAG_GC;
        else if (*w == "seq")
            showFlag = FLAG_SEQ;
        else if (*w == "attr")
            showFlag = FLAG_ATTR;
        else if (*w == "crtc")
            showFlag = FLAG_CRTC;
        else if (*w == "ext")
            showFlag = FLAG_EXT;
        else if (*w == "mode")
            showFlag = FLAG_MODE;
        else
            throw std::runtime_error { std::format("Unknown VGA command \"{}\"", *w) };
    }
    if (showFlag & FLAG_SEQ)
        ShowRegisters("Sequencer", seqReg_, seqRegName);
    if (showFlag & FLAG_CRTC)
        ShowRegisters("CRTC", crtcReg_, crtcRegName);
    if (showFlag & FLAG_EXT) {
        std::println("External registers:");
        std::println("Misc out. {:02X} 0b{:08b}", miscOut_, miscOut_);
        // Feature control/ Graphics position..
    }
    if (showFlag & FLAG_ATTR)
        ShowRegisters("Attribute", attrReg_, attrRegName);
    if (showFlag & FLAG_GC)
        ShowRegisters("Graphics controller", gcReg_, gcRegName);
    if (showFlag & FLAG_MODE)
        displayInfo_.log(!(gcReg_[GC_REG_MISC] & GC_MISC_MASK_ALPHA_DIS), attrReg_[ATTR_REG_PLANE_ENABLE]);
}

VGA::VGA(SystemBus& bus)
    : impl_{ std::make_unique<impl>(bus) }
{
}

VGA::~VGA() = default;

void VGA::forceRedraw()
{
    impl_->renderFrame();
}

void VGA::setDrawFunction(const DrawFunction& onDraw)
{
    impl_->setDrawFunction(onDraw);
}

void VGA::registerDebugFunction(class Debugger& dbg)
{
    impl_->registerDebugFunction(dbg);
}