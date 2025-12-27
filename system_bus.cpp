#include "system_bus.h"
#include <format>
#include <print>
#include <stdexcept>
#include <utility>

std::uint8_t IOHandler::inU8(std::uint16_t port, std::uint16_t)
{
    std::println("Unspported 8-bit I/O input from port 0x{:04X}", port);
    THROW_FLIPFLOP();
    return 0xFF;
}

std::uint16_t IOHandler::inU16(std::uint16_t port, std::uint16_t offset)
{
    uint16_t low = inU8(port, offset);
    return low | inU8(port + 1, offset + 1) << 8;
}

std::uint32_t IOHandler::inU32(std::uint16_t port, std::uint16_t offset)
{
    uint32_t low = inU16(port, offset);
    return low | inU16(port + 2, offset + 2) << 16;
}

void IOHandler::outU8(std::uint16_t port, std::uint16_t, std::uint8_t value)
{
    std::println("Unspported 8-bit I/O output to port 0x{:04X} value=0x{:02X}", port, value);
    THROW_FLIPFLOP();
}

void IOHandler::outU16(std::uint16_t port, std::uint16_t offset, std::uint16_t value)
{
    // Automatically split the write
    outU8(port, offset, static_cast<uint8_t>(value));
    outU8(port + 1, offset + 1, static_cast<uint8_t>(value >> 8));
}

void IOHandler::outU32(std::uint16_t port, std::uint16_t offset, std::uint32_t value)
{
    outU16(port, offset, static_cast<uint16_t>(value));
    outU16(port + 2, offset + 2, static_cast<uint16_t>(value >> 16));
}

template <bool ReadOnly>
void DefaultMemHandler<ReadOnly>::writeU8(std::uint64_t addr, std::uint64_t offset, std::uint8_t value)
{
    if constexpr (ReadOnly) {
        // IBM PC XT BIOS pushes with SS=F000
        std::println("Write to ROM addr {:X} value {:02X}", addr, value);
        //throw std::runtime_error { "XXX" };
    } else {
        assert(offset < data_.size());
        data_[offset] = value;
    }
}

template class DefaultMemHandler<false>;
template class DefaultMemHandler<true>;

template <typename T>
T SystemBus::read(std::uint64_t addr)
{
    addCycles(sizeof(T));
    addr &= addressMask_;
    if (auto ah = findHandler(memHandlers_, addr); ah) {
        if (ah->needSync)
            runCycles();
        if constexpr (sizeof(T) == 1)
            return ah->handler->readU8(addr, addr - ah->base);
        else if constexpr (sizeof(T) == 2)
            return ah->handler->readU16(addr, addr - ah->base);
        else if constexpr (sizeof(T) == 4)
            return ah->handler->readU32(addr, addr - ah->base);
        else {
            static_assert(sizeof(T) == 8);
            return ah->handler->readU64(addr, addr - ah->base);
        }
    }
    std::println("Read of size {} from unmmaped address {:X}", sizeof(T), addr);
    if (addr == 0xC9FF8)
        throw std::runtime_error { "XXX" };
    if constexpr (sizeof(T) == 1)
        return 0xF4; // HLT opcode
    else
        return static_cast<T>(~T(0));
}

template <typename T>
void SystemBus::write(std::uint64_t addr, T value)
{
    addCycles(sizeof(T));
    addr &= addressMask_;

    #if 0
    const auto watchAddr = 0x0038FFFD;
    if (addr <= watchAddr && addr + sizeof(T) - 1 >= watchAddr) {
        std::println(">>>>>>>>> Write of size {} to address {:X} value={:0{}X}", sizeof(T), addr, value, sizeof(T) * 2);
        THROW_ONCE();
    }
    #endif

    if (auto ah = findHandler(memHandlers_, addr); ah) {
        if (ah->needSync)
            runCycles();
        if constexpr (sizeof(T) == 1)
            ah->handler->writeU8(addr, addr - ah->base, value);
        else if constexpr (sizeof(T) == 2)
            ah->handler->writeU16(addr, addr - ah->base, value);
        else {
            static_assert(sizeof(T) == 4);
            ah->handler->writeU32(addr, addr - ah->base, value);
        }
    } else {
        THROW_ONCE();
        std::println("Write of size {} to unmmaped address {:X} value={:0{}X}", sizeof(T), addr, value, sizeof(T)*2);
        //if (addr <1024*1024)
        //    throw std::runtime_error { std::format("Write of size {} to unmmaped address {:X} value={:0{}X}", sizeof(T), addr, value, sizeof(T) * 2) };
    }
}

void SystemBus::recalcNextAction()
{
    nextAction_ = UINT64_MAX;
    for (auto& obs : cycleObservers_)
        nextAction_ = std::min(nextAction_, obs->nextAction());
}

void SystemBus::addCycles(std::uint64_t count)
{
    count *= 2; // Fudge factor...
    cycles_ += count;
    if (cycles_ >= nextAction_)
        runCycles();
}

void SystemBus::runCycles()
{
    // Originally the system clock was 14.31818 MHz, /3 -> 4.77MHz for the CPU and /4 -> 3.579545 MHz for NTSC
    const auto cycles = std::exchange(cycles_, 0) * 3;
    for (auto& obs : cycleObservers_)
        obs->runCycles(cycles);
    recalcNextAction();
}


template std::uint8_t SystemBus::read<std::uint8_t>(std::uint64_t addr);
template std::uint16_t SystemBus::read<std::uint16_t>(std::uint64_t addr);
template std::uint32_t SystemBus::read<std::uint32_t>(std::uint64_t addr);
template std::uint64_t SystemBus::read<std::uint64_t>(std::uint64_t addr);

template void SystemBus::write(std::uint64_t addr, std::uint8_t value);
template void SystemBus::write(std::uint64_t addr, std::uint16_t value);
template void SystemBus::write(std::uint64_t addr, std::uint32_t value);