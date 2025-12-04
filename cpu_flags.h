#ifndef CPU_FLAGS_H
#define CPU_FLAGS_H

#include <cstdint>
#include <utility>

enum {
    EFLAGS_BIT_CF,
    EFLAGS_BIT_RES0, // Always 1
    EFLAGS_BIT_PF,
    EFLAGS_BIT_RES1,
    EFLAGS_BIT_AF,
    EFLAGS_BIT_RES2,
    EFLAGS_BIT_ZF,
    EFLAGS_BIT_SF,
    EFLAGS_BIT_TF,
    EFLAGS_BIT_IF,
    EFLAGS_BIT_DF,
    EFLAGS_BIT_OF,
    EFLAGS_BIT_IOPL,
    EFLAGS_BIT_NT = 14,
    EFLAGS_BIT_VM = 17,
};
static_assert(EFLAGS_BIT_OF == 11);

constexpr std::uint32_t EFLAGS_MASK_CF = 1 << EFLAGS_BIT_CF; // 0x0000`0001
constexpr std::uint32_t EFLAGS_MASK_PF = 1 << EFLAGS_BIT_PF; // 0x0000`0004
constexpr std::uint32_t EFLAGS_MASK_AF = 1 << EFLAGS_BIT_AF; // 0x0000`0010
constexpr std::uint32_t EFLAGS_MASK_ZF = 1 << EFLAGS_BIT_ZF; // 0x0000`0040
constexpr std::uint32_t EFLAGS_MASK_SF = 1 << EFLAGS_BIT_SF; // 0x0000`0080
constexpr std::uint32_t EFLAGS_MASK_TF = 1 << EFLAGS_BIT_TF; // 0x0000`0100
constexpr std::uint32_t EFLAGS_MASK_IF = 1 << EFLAGS_BIT_IF; // 0x0000`0200
constexpr std::uint32_t EFLAGS_MASK_DF = 1 << EFLAGS_BIT_DF; // 0x0000`0400
constexpr std::uint32_t EFLAGS_MASK_OF = 1 << EFLAGS_BIT_OF; // 0x0000`0800
constexpr std::uint32_t EFLAGS_MASK_IOPL = 3 << EFLAGS_BIT_IOPL; // 0x0000`3000
constexpr std::uint32_t EFLAGS_MASK_NT = 1 << EFLAGS_BIT_NT; // 0x0000`4000
constexpr std::uint32_t EFLAGS_MASK_VM = 1 << EFLAGS_BIT_VM; // 0x0002`0000

static inline bool EvalCond(std::uint32_t flags, std::uint8_t cond)
{
    uint8_t res;
    cond &= 0xf;
    switch (cond >> 1) {
    case 0: // JO/JNO
        res = (flags & EFLAGS_MASK_OF) != 0;
        break;
    case 1: // JB/JNB
        res = (flags & EFLAGS_MASK_CF) != 0;
        break;
    case 2: // JZ/JNZ
        res = (flags & EFLAGS_MASK_ZF) != 0;
        break;
    case 3: // JBE/JBNE
        res = (flags & EFLAGS_MASK_CF) || (flags & EFLAGS_MASK_ZF);
        break;
    case 4: // JS/JNS
        res = (flags & EFLAGS_MASK_SF) != 0;
        break;
    case 5: // JP/JNP
        res = (flags & EFLAGS_MASK_PF) != 0;
        break;
    case 6: // JL/JNL (SF!=OF)
        res = ((flags >> EFLAGS_BIT_SF) ^ (flags >> EFLAGS_BIT_OF)) & 1;
        break;
    case 7: // JLE/JNLE ((ZF=1) OR (SF!=OF))
        res = (flags & EFLAGS_MASK_ZF) || (((flags >> EFLAGS_BIT_SF) ^ (flags >> EFLAGS_BIT_OF)) & 1);
        break;
    default:
        std::unreachable();
    }
    return res != (cond & 1);
}


#endif
