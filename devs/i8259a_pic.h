#ifndef I8259A_PIC
#define I8259A_PIC

#include "system_bus.h"

class i8259a_PIC : public IOHandler {
public:
    explicit i8259a_PIC(SystemBus& bus, uint16_t ioBase);

    void reset();

    std::uint8_t inU8(uint16_t port, uint16_t offset) override;
    void outU8(uint16_t port, uint16_t offset, std::uint8_t value) override;

    int getInterrupt(); // -1 -> No interrupt
    void setInterrupt(std::uint8_t line);
    void clearInterrupt(std::uint8_t line);

private:
    uint8_t icwCnt_; // Initialization Command Words (sequence)
    uint8_t icw1_;
    uint8_t icw2_; // Base
    uint8_t icw3_; // Slave attached to corresponding interrupt pin
    uint8_t icw4_;

    uint8_t irr_; // Interrupt Request Register
    uint8_t isr_; // In-Service Register
    uint8_t imr_; // Interrupt Mask Register

    uint8_t nextReg_;
};

enum : uint8_t {
    PIC_IRQ_PIT,
    PIC_IRQ_KEYBOARD,
    PIC_IRQ_CASCADE,
    PIC_IRQ_COM2,
    PIC_IRQ_COM1,
    PIC_IRQ_LPT2,
    PIC_IRQ_FLOPPY,
    PIC_IRQ_LPT1,
};

#endif
