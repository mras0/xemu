#ifndef SYSTEM_BUS_H
#define SYSTEM_BUS_H

#include <cstdint>
#include <vector>
#include <stdexcept>
#include <algorithm>
#include <cassert>
#include "util.h"

class BusDevice {
    virtual ~BusDevice() { }

    virtual void reset() { }
};

class MemoryHandler {
public:
    virtual ~MemoryHandler() { }

    virtual std::uint8_t readU8(std::uint64_t addr, std::uint64_t offset) = 0;

    virtual std::uint8_t peekU8(std::uint64_t addr, std::uint64_t offset)
    {
        return readU8(addr, offset);
    }

    virtual std::uint16_t readU16(std::uint64_t addr, std::uint64_t offset)
    {
        std::uint16_t res = readU8(addr, offset);
        res |= readU8(addr + 1, offset + 1) << 8;
        return res;
    }

    virtual std::uint32_t readU32(std::uint64_t addr, std::uint64_t offset)
    {
        std::uint32_t res = readU16(addr, offset);
        res |= readU16(addr + 2, offset + 2) << 16;
        return res;
    }

    virtual std::uint64_t readU64(std::uint64_t addr, std::uint64_t offset)
    {
        std::uint64_t res = readU32(addr, offset);
        res |= static_cast<uint64_t>(readU32(addr + 4, offset + 4)) << 32;
        return res;
    }

    virtual void writeU8(std::uint64_t addr, std::uint64_t offset, std::uint8_t value) = 0;

    virtual void writeU16(std::uint64_t addr, std::uint64_t offset, std::uint16_t value)
    {
        writeU8(addr, offset, value & 0xff);
        writeU8(addr + 1, offset + 1, value >> 8);
    }

    virtual void writeU32(std::uint64_t addr, std::uint64_t offset, std::uint32_t value)
    {
        writeU16(addr, offset, value & 0xffff);
        writeU16(addr + 2, offset + 2, value >> 16);
    }
};

class IOHandler {
public:
    virtual ~IOHandler() { }

    virtual std::uint8_t inU8(std::uint16_t port, std::uint16_t offset);
    virtual std::uint16_t inU16(std::uint16_t port, std::uint16_t offset);
    virtual std::uint32_t inU32(std::uint16_t port, std::uint16_t offset);

    virtual void outU8(std::uint16_t port, std::uint16_t offset, std::uint8_t value);
    virtual void outU16(std::uint16_t port, std::uint16_t offset, std::uint16_t value);
    virtual void outU32(std::uint16_t port, std::uint16_t offset, std::uint32_t value);
};

template<bool ReadOnly>
class DefaultMemHandler : public MemoryHandler {
public:
    DefaultMemHandler(std::vector<uint8_t>&& data)
        : data_ { data }
    {
    }

    std::vector<uint8_t>& data()
    {
        return data_;
    }

    std::size_t size() const
    {
        return data_.size();
    }

    std::uint8_t readU8(std::uint64_t, std::uint64_t offset)
    {
        assert(offset + 1 <= data_.size());
        return data_[offset];
    }

    std::uint16_t readU16(std::uint64_t, std::uint64_t offset)
    {
        assert(offset + 2 <= data_.size());
        return *(const std::uint16_t*)&data_[offset];
    }

    std::uint32_t readU32(std::uint64_t, std::uint64_t offset)
    {
        assert(offset + 4 <= data_.size());
        return *(const std::uint32_t*)&data_[offset];
    }

    void writeU8(std::uint64_t addr, std::uint64_t offset, std::uint8_t value);

private:
    std::vector<uint8_t> data_;
};

class RomHandler : public DefaultMemHandler<true> {
public:
    explicit RomHandler(const std::vector<uint8_t>& data)
        : DefaultMemHandler { std::vector<uint8_t>(data) }
    {
    }

};

class RamHandler : public DefaultMemHandler<false> {
public:
    explicit RamHandler(std::size_t size)
        : DefaultMemHandler { std::vector<uint8_t>(size, 0) }
    {
    }
};

class UnmappedMemHandler : public MemoryHandler {
public:
    std::uint8_t readU8(std::uint64_t, std::uint64_t) override
    {
        return 0xFF;
    }

    void writeU8(std::uint64_t, std::uint64_t, std::uint8_t) override { }
};

class CycleObserver {
public:
    virtual void runCycles(std::uint64_t numCycles) = 0;
    virtual std::uint64_t nextAction() { return UINT64_MAX; }
};

// TODO: Handle case where something straddles two areas
class SystemBus {
public:
    explicit SystemBus() {}

    void addMemHandler(std::uint64_t base, std::uint64_t length, MemoryHandler& handler, bool needSync = false)
    {
        addHandler(memHandlers_, AreaHandler { base, length, &handler, needSync });
    }

    void addIOHandler(std::uint16_t base, std::uint16_t length, IOHandler& handler, bool needSync = false)
    {
        addHandler(ioHandlers_, AreaHandler { base, length, &handler, needSync });
    }

    void setDefaultIOHandler(IOHandler* handler)
    {
        defaultIoHandler_.handler = handler;
    }

    void addCycleObserver(CycleObserver& obs)
    {
        cycleObservers_.push_back(&obs);
    }

    void setAddressMask(uint64_t mask)
    {
        addressMask_ = mask;
    }

    template <typename T>
    T read(std::uint64_t addr);

    template <typename T>
    void write(std::uint64_t addr, T value);

    std::uint8_t peekU8(std::uint64_t addr)
    {
        addr &= addressMask_;
        if (auto ah = findHandler(memHandlers_, addr); ah)
            return ah->handler->peekU8(addr, addr - ah->base);
        throw std::runtime_error { "No handler for peek from 0x" + HexString(addr) };
    }

    std::uint8_t readU8(std::uint64_t addr)
    {
        return read<uint8_t>(addr);
    }

    std::uint16_t readU16(std::uint64_t addr)
    {
        return read<uint16_t>(addr);
    }

    std::uint32_t readU32(std::uint64_t addr)
    {
        return read<uint32_t>(addr);
    }

    std::uint64_t readU64(std::uint64_t addr)
    {
        return read<uint64_t>(addr);
    }

    void writeU8(std::uint64_t addr, std::uint8_t value)
    {
        return write<uint8_t>(addr, value);
    }

    void writeU16(std::uint64_t addr, std::uint16_t value)
    {
        return write<uint16_t>(addr, value);
    }

    void writeU32(std::uint64_t addr, std::uint32_t value)
    {
        return write<uint32_t>(addr, value);
    }

    void ioOutput(std::uint16_t port, std::uint32_t value, std::uint8_t size)
    {
        assert(size == 1 || size == 2 || size == 4);
        addCycles(1);
        auto ah = findHandler(ioHandlers_, port);
        if (!ah && defaultIoHandler_.handler)
            ah = &defaultIoHandler_;
        if (ah) {
            if (ah->needSync)
                runCycles();
            const auto offset = static_cast<uint16_t>(port - ah->base);
            if (size == 1)
                ah->handler->outU8(port, offset, static_cast<uint8_t>(value));
            else if (size == 2)
                ah->handler->outU16(port, offset, static_cast<uint16_t>(value));
            else
                ah->handler->outU32(port, offset, value);
        } else {
            throw std::runtime_error { "No handler for io output of size " + std::to_string(size) + " to 0x" + HexString(port) + " value " + HexString(value, 2 * size) };
        }
    }

    std::uint32_t ioInput(std::uint16_t port, std::uint8_t size)
    {
        assert(size == 1 || size == 2 || size == 4);
        addCycles(1);
        auto ah = findHandler(ioHandlers_, port);
        if (!ah && defaultIoHandler_.handler)
            ah = &defaultIoHandler_;
        if (ah) {
            if (ah->needSync)
                runCycles();
            const auto offset = static_cast<uint16_t>(port - ah->base);
            if (size == 1)
                return ah->handler->inU8(port, offset);
            else if (size == 2)
                return ah->handler->inU16(port, offset);
            else
                return ah->handler->inU32(port, offset);
        } else {
            throw std::runtime_error { "No handler for io input of size " + std::to_string(size) + " from 0x" + HexString(port)};
        }
    }

    void recalcNextAction();
    void addCycles(std::uint64_t count);
    void runCycles();

private:
    template<typename T, typename L>
    struct AreaHandler {
        L base;
        L length;
        T* handler;
        bool needSync;
    };
    using MemHandlerType = AreaHandler<MemoryHandler, std::uint64_t>;
    using IOHandlerType = AreaHandler<IOHandler, std::uint16_t>;

    std::vector<MemHandlerType> memHandlers_;
    std::vector<IOHandlerType> ioHandlers_;
    std::vector<CycleObserver*> cycleObservers_;
    IOHandlerType defaultIoHandler_ {};
    std::uint64_t addressMask_ = 0xfffff;
    std::uint64_t cycles_ = 0;
    std::uint64_t nextAction_ = 0;

    template <typename T, typename L>
    static void addHandler(std::vector<AreaHandler<T, L>>& handlers, AreaHandler<T, L>&& handler)
    {
        // TODO: Check for overlap
        // TODO: Could use binary search
        auto it = std::find_if(handlers.begin(), handlers.end(), [=](const auto& ah) {
            return ah.base > handler.base;
        });
        handlers.insert(it, handler);
    }

    template <typename T, typename L>
    static AreaHandler<T, L>* findHandler(std::vector<AreaHandler<T, L>>& handlers, L addr)
    {
        // TODO: Smarter search
        auto it = std::find_if(handlers.begin(), handlers.end(), [=](const auto& ah) {
            return addr >= ah.base && addr < ah.base + ah.length;
        });
        if (it == handlers.end())
            return nullptr;
        return &*it;
    }
};


#endif
