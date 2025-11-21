#include "CGA.h"
#include <print>
#include <cstring>

namespace {

// Base clock = System/4 = 3.579545MHz. Lines per seconds = base clock * 2/455Mhz = 9/572MHz (~15734). 30/1.001 ~=29.970 FPS

constexpr uint32_t linesPerFrame = 525; // 2 fields of 262.5
constexpr uint32_t cyclesPerLine2 = 455; // 227.5 cycles per line
constexpr uint32_t cyclesPerLineSys = cyclesPerLine2 * 2;
constexpr uint32_t hsyncSys = 160 * 4;

constexpr uint32_t cyclesPerFrame2 = linesPerFrame * cyclesPerLine2;
constexpr uint32_t cyclesPerFrameSys = 2 * cyclesPerFrame2;
constexpr uint32_t vsyncStartSys = 400 * cyclesPerLine2 * 2;

enum MC6845RegisterIndex {
    HorTotal,
    HorDisp,
    HorSyncPos,
    HorSyncWidth,
    VertTotal,
    VertDisp,
    VertSyncPos,
    VertSyncWidth,
    InterlaceMode,
    MaxScanLineAddress,
    CursorStart,
    CursorEnd,
    StartAddressH,
    StartAddressL,
    CursorAddressH,
    CursorAddressL,
    LightPenH,
    LightPenL,

    Max
};
static_assert(static_cast<int>(MC6845RegisterIndex::Max) == 18);

// 03D8 - Mode Control Register(Write Only)
constexpr uint8_t MCR_MASK_TEXT_CLOUMNS = 1 << 0; // 0 = 40*25 text mode, 1 = 80*25 text mode
constexpr uint8_t MCR_MASK_GRAPHICS = 1 << 1; // 0 = text mode, 1 = graphics mode
constexpr uint8_t MCR_MASK_MONOCHROME = 1 << 2; // 0 = color, 1 = monochome
constexpr uint8_t MCR_MASK_VIDEO_ENABLE = 1 << 3;
constexpr uint8_t MCR_MASK_HIRES = 1 << 4; // 0 = 320*200, 1 =  640*200 graphics mode
constexpr uint8_t MCR_MASK_BLINK = 1 << 5; // 0 = blink enabled/8 background colors, 1 = blink disabled/16 background colors

// 03DA - Status Register (Read Only)
constexpr uint8_t STAT_MASK_DISPLAY_INACTIVE = 1 << 0;
constexpr uint8_t STAT_MASK_LP_TRIGGER = 1 << 1; // 1 = inactive
constexpr uint8_t STAT_MASK_LP_SWITCH = 1 << 2; // 1 = off
constexpr uint8_t STAT_MASK_VSYNC_ACTIVE = 1 << 3;

constexpr uint32_t cgaPalette[16] = {
    0x000000, // black
    0x0000AA, // blue
    0x00AA00, // green
    0x00AAAA, // cyan
    0xAA0000, // red
    0xAA00AA, // magenta
    0xAA5500, // brown ("dark yellow")
    0xAAAAAA, // light gray
    0x555555, // dark gray
    0x5555FF, // light blue
    0x55FF55, // light green
    0x55FFFF, // light cyan
    0xFF5555, // light red
    0xFF55FF, // light magenta
    0xFFFF55, // yellow
    0xFFFFFF, // white
};

constexpr uint8_t cgaMode4Palettes[4][4] = {
    // Palette 0
    { 0, 2, 4, 6 }, // low intensity
    { 0, 10, 12, 14 }, //high intensity
    // Palette 1
    { 0, 3, 5, 7 }, // low intensity
    { 0, 11, 12, 15 }, // high intensity
};

#include "cga_font.h"

} // unnamed namespace

class CGA::impl : public CycleObserver, public IOHandler {
public:
    explicit impl(SystemBus& bus);


    void setDrawFunction(const DrawFunction& onDraw)
    {
        onDraw_ = onDraw;
    }

    void reset();
    void runCycles(std::uint64_t numCycles) override;
    std::uint64_t nextAction() override;

    std::uint8_t inU8(uint16_t port, uint16_t) override;
    void outU8(uint16_t port, uint16_t, std::uint8_t value) override;

private:
    DrawFunction onDraw_;
    RamHandler videoMem_;

    std::vector<uint32_t> pixels_;

    uint64_t cycles_;
    uint32_t numFrames_;
    uint8_t mcr_;
    uint8_t palette_;

    uint8_t registerIndex_;
    uint8_t mc6845Registers_[static_cast<int>(MC6845RegisterIndex::Max)];

    void render();
};

CGA::impl::impl(SystemBus& bus)
    : videoMem_ { 16 * 1024 }
{
    bus.addCycleObserver(*this);
    bus.addIOHandler(0x3D0, 0x10, *this, true);
    bus.addMemHandler(0xB8000, videoMem_.size(), videoMem_); // TODO: needSync to implement snow
    //bus.addMemHandler(0xBC000, videoMem_.size(), videoMem_); // Mirrored

    reset();
}

void CGA::impl::reset()
{
    cycles_ = 0;
    numFrames_ = 0;
    mcr_ = 0;
    palette_ = 0;
    registerIndex_ = 0;
    std::memset(mc6845Registers_, 0, sizeof(mc6845Registers_));
}

void CGA::impl::runCycles(std::uint64_t numCycles)
{
    cycles_ += numCycles;
    for (; cycles_ >= cyclesPerFrameSys; cycles_ -= cyclesPerFrameSys) {
        if (mcr_ & MCR_MASK_VIDEO_ENABLE) {
            render();
            ++numFrames_;
        }
    }
}

std::uint64_t CGA::impl::nextAction()
{
    assert(cycles_ <= cyclesPerFrameSys);
    return cyclesPerFrameSys - cycles_;
}

void CGA::impl::render()
{
    const auto startAddress = mc6845Registers_[MC6845RegisterIndex::StartAddressH] << 8 | mc6845Registers_[MC6845RegisterIndex::StartAddressL];
    if (startAddress)
        throw std::runtime_error { std::format("TODO: CGA render with mcr=0x{:2X} startAddress=0x{:X}", mcr_, startAddress) };

    auto vidMem = &videoMem_.data()[0];

    if (mcr_ & MCR_MASK_GRAPHICS) {
        const int screenH = 200;
        if (mcr_ & MCR_MASK_HIRES) {
            const int screenW = 640;
            pixels_.resize(screenW * screenH);
            const auto fg = cgaPalette[palette_ & 0xf];

            for (int y = 0; y < screenH; ++y) {
                for (int x = 0; x < screenW; x += 8) {
                    auto px = vidMem[(x >> 3) + (y >> 1) * 80 + ((y & 1) << 13)];
                    for (int sx = 0; sx < 8; ++sx) {
                        pixels_[x + sx + y * screenW] = px & 0x80 ? fg : 0;
                        px <<= 1;
                    }
                }
            }

            onDraw_(pixels_.data(), screenW, screenH);
        } else {
            const int screenW = 320;
            pixels_.resize(screenW * screenH);

            const auto pal = cgaMode4Palettes[(palette_ >> 4) & 3];
            const uint32_t colors[4] = {
                cgaPalette[palette_ & 0xf],
                cgaPalette[pal[1]],
                cgaPalette[pal[2]],
                cgaPalette[pal[3]],
            };

            for (int y = 0; y < screenH; ++y) {
                for (int x = 0; x < screenW; ++x) {
                    auto px = vidMem[(x >> 2) + (y >> 1) * 80 + ((y & 1) << 13)] >> 2 * (3 - (x & 3));
                    pixels_[x + y * screenW] = colors[px & 3];
                }
            }
            onDraw_(pixels_.data(), screenW, screenH);
        }
    } else {
        if ((mcr_ & ~MCR_MASK_TEXT_CLOUMNS) != (MCR_MASK_VIDEO_ENABLE | MCR_MASK_BLINK))
            throw std::runtime_error { std::format("TODO: CGA render with mcr=0x{:2X} 0b{:08b}", mcr_, mcr_) };

        const int textW = mcr_ & MCR_MASK_TEXT_CLOUMNS ? 80 : 40;
        const int textH = 25;
        const int charW = 8;
        const int charH = 8;
        const int screenW = textW * charW;
        const int screenH = textH * charH;
        pixels_.resize(screenW * screenH);

        for (int ty = 0, bufIdx = 0; ty < textH; ++ty) {
            for (int tx = 0; tx < textW; ++tx, bufIdx += 2) {
                const auto ch = vidMem[bufIdx];
                const auto attr = vidMem[bufIdx + 1];
                const auto fg = cgaPalette[attr & 15];
                const auto bg = cgaPalette[attr >> 4];

                auto pix = &pixels_[tx * charW + screenW * ty * charH];
                auto fnt = &cgaFont[ch * 8];
                for (int y = 0; y < charH; ++y) {
                    for (int x = 0; x < charW; ++x) {
                        pix[x + y * screenW] = (fnt[y] << x) & 0x80 ? fg : bg;
                    }
                }
            }
        }

        const auto cursorAddress = mc6845Registers_[MC6845RegisterIndex::CursorAddressH] << 8 | mc6845Registers_[MC6845RegisterIndex::CursorAddressL];
        const int cursorX = cursorAddress % textW;
        const int cursorY = cursorAddress / textW;
        const int cursorStart = mc6845Registers_[MC6845RegisterIndex::CursorStart] & 0x1f; // bits 5 and 6 control the blink rate
        const int cursorEnd = mc6845Registers_[MC6845RegisterIndex::CursorEnd] & 0x1f;
        if (cursorX < textW && cursorY < textH && ((numFrames_ >> 4) & 1)) { // Cursor blinks every 16th frame (VGA maybe controlled by bits in R10 for CGA)
            auto pix = &pixels_[cursorX * charW + screenW * cursorY * charH];
            const auto color = cgaPalette[vidMem[2 * (cursorX + cursorY * textW) + 1] & 15]; // The cursor takes its color from the foreground attribute;
            for (int y = cursorStart; y <= cursorEnd && y < charH; ++y) {
                for (int x = 0; x < charW; ++x) {
                    pix[x + y * screenW] = color;
                }
            }
        }
        onDraw_(pixels_.data(), screenW, screenH);
    }
}

std::uint8_t CGA::impl::inU8(uint16_t port, uint16_t)
{
    uint8_t value = 0;
    switch (port) {
    case 0x3DA:
        value = STAT_MASK_LP_TRIGGER | STAT_MASK_LP_SWITCH;
        if (cycles_ >= vsyncStartSys)
            value |= STAT_MASK_VSYNC_ACTIVE;
        if (cycles_ % cyclesPerLineSys >= hsyncSys)
            value |= STAT_MASK_DISPLAY_INACTIVE;
        break;
    default:
        return IOHandler::inU8(port, 0);
    }
    return value;
}

void CGA::impl::outU8(uint16_t port, uint16_t, std::uint8_t value)
{
    switch (port) {
    case 0x3D0:
    case 0x3D2:
    case 0x3D4:
    case 0x3D6:
        registerIndex_ = value & 0x1f;
        break;
    case 0x3D1:
    case 0x3D3:
    case 0x3D5:
    case 0x3D7:
        if (registerIndex_ < static_cast<int>(MC6845RegisterIndex::Max)) {
            if (registerIndex_ != MC6845RegisterIndex::CursorAddressH && registerIndex_ != MC6845RegisterIndex::CursorAddressL)
                std::println("CGA write to register {} 0x{:02X}", registerIndex_, value);
            mc6845Registers_[registerIndex_] = value;
        } else {
            throw std::runtime_error { std::format("Write to invalid CGA MC6845 register {} value 0x{:02X}", registerIndex_, value) };
        }
        break;
    case 0x3D8:
        std::println("CGA MCR={:02X} 0b{:08b}", value, value);
        mcr_ = value;
        if (!(mcr_ & MCR_MASK_VIDEO_ENABLE))
            onDraw_(nullptr, 0, 0);
        break;
    case 0x3D9:
        std::println("CGA CGA palette register={:02X}", value);
        palette_ = value;
        break;
    default:
        IOHandler::outU8(port, 0, value);
    }
}


CGA::CGA(SystemBus& bus)
    : impl_ { std::make_unique<impl>(bus) }
{
}

CGA::~CGA() = default;

void CGA::setDrawFunction(const DrawFunction& onDraw)
{
    impl_->setDrawFunction(onDraw);
}