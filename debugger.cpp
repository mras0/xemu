#include "debugger.h"
#include "util.h"

#include <iostream>
#include <string_view>
#include <optional>
#include <print>
#include <variant>
#include <csignal>
#include <climits>

namespace {

char ToUpper(char ch)
{
    return ch >= 'a' && ch <= 'z' ? ch & ~0x20 : ch;
}

std::string ToUpperStr(std::string_view id)
{
    std::string upperId;
    for (auto c : id)
        upperId.push_back(ToUpper(c));
    return upperId;
}

bool IsSpace(char ch)
{
    return ch == ' ' || ch == '\t';
}

bool IsDigit(char ch)
{
    return ch >= '0' && ch <= '9';
}

bool IsAlpha(char ch)
{
    return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z');
}

bool IsAlnum(char ch)
{
    return IsDigit(ch) || IsAlpha(ch);
}

bool IsIdContChar(char ch)
{
    return IsAlnum(ch) || ch == '_';
}

bool IsNumberSeparator(char ch)
{
    return ch == '`';
}

enum : unsigned {
    OP_LSH = 256,
    OP_RSH,
};

std::string OperatorString(unsigned op)
{
    if (!op)
        return "Zero-operator";
    if (op <= 255)
        return std::string(1, (char)op);
    switch (op) {
    case OP_LSH:
        return "<<";
    case OP_RSH:
        return ">>";
    default:
        return "Invalid operator (" + std::to_string(op) + ")";
    }
}

constexpr unsigned MAX_PRECEDENCE = 100;

unsigned OperatorPrecedence(unsigned op)
{
    switch (op) {
    case 0:
        return UINT_MAX;
    case '*':
    case '/':
    case '%':
        return 5;
    case '+':
    case '-':
        return 6;
    case OP_LSH:
    case OP_RSH:
        return 7;
    case '&':
        return 11;
    case '^':
        return 12;
    case '|':
        return 13;
    default:
        throw std::runtime_error { std::format("Invalid operator in OperatorPrecedence: {}", OperatorString(op)) };
    }
}

void HexDump(Address addr, size_t size, std::function<std::optional<std::uint8_t> (uint64_t offset)> peek)
{
    constexpr size_t incr = 16;
    uint64_t offset = 0;

    while (size) {
        const auto here = std::min(incr, size);
        std::optional<std::uint8_t> buffer[16] {};
        for (size_t i = 0; i < here; ++i)
            buffer[i] = peek(offset++);

        std::print("{} ", addr);
        for (size_t i = 0; i < here; ++i) {
            if (buffer[i])
                std::print(" {:02x}", *buffer[i]);
            else
                std::print(" ??");
        }
        for (size_t i = here; i < incr; ++i)
            std::print("   ");
        std::print("  ");
        for (size_t i = 0; i < here; ++i)
            std::print("{}", buffer[i] && *buffer[i] >= ' ' && *buffer[i] < 127 ? static_cast<char>(*buffer[i]) : '.');
        std::print("\n");
        addr += here;
        size -= here;
    }
}

static const int SREG_INVALID = -1;

using RegValueType = std::pair<uint64_t*, unsigned>;

static int SRegLookup(std::string_view upperId)
{
    for (int i = 0; i < 6; ++i)
        if (upperId == SRegText[i])
            return i;
    return SREG_INVALID;
}

static RegValueType RegLookup(CPUState& st, std::string_view id)
{
    const std::string upperId = ToUpperStr(id);

    if (auto sr = SRegLookup(upperId); sr != SREG_INVALID)
        return { reinterpret_cast<uint64_t*>(&st.sregs_[sr]), 16 };

    for (int i = 0; i < 8; ++i) {
        if (upperId == Reg8Text[i])
            return { &st.regs_[i & 3], i & 4 };
        if (upperId == Reg16Text[i])
            return { &st.regs_[i], 16 };
        if (upperId == Reg32Text[i])
            return { &st.regs_[i], 32 };
    }

    if (upperId == "IP")
        return { &st.ip_, 16 };
    else if (upperId == "EIP")
        return { &st.ip_, 32 };

    return { nullptr, 0 };
}

std::uint64_t RegGet(RegValueType rt)
{
    assert(rt.first);
    switch (rt.second) {
    case 0:
        return *rt.first & 0xff;
    case 4:
        return (*rt.first >> 8) & 0xff;
    case 16:
        return *rt.first & 0xffff;
    case 32:
        return *rt.first & 0xffffffff;
    default:
        throw std::runtime_error { std::format("TODO: RegGet info {}", rt.second) };
    }
}

void RegSet(RegValueType rt, std::uint64_t value)
{
    assert(rt.first);
    switch (rt.second) {
    case 0:
        reinterpret_cast<uint8_t*>(rt.first)[0] = static_cast<uint8_t>(value);
        break;
    case 4:
        reinterpret_cast<uint8_t*>(rt.first)[1] = static_cast<uint8_t>(value);
        break;
    case 16:
        reinterpret_cast<uint16_t*>(rt.first)[0] = static_cast<uint16_t>(value);
        break;
    case 32:
        reinterpret_cast<uint32_t*>(rt.first)[0] = static_cast<uint32_t>(value);
        break;
    default:
        throw std::runtime_error { std::format("TODO: RegSet info {} value 0x{:X}", rt.second, value) };
    }
}

volatile bool breakActivated;

void BreakHandler(int)
{
    breakActivated = true;
}

void InstallBreakHandler(void)
{
    breakActivated = false;
    signal(SIGINT, &BreakHandler);
}

uint64_t GetPhysicalIp(const CPUState& st)
{
    assert(!st.protectedMode()); // TODO
    return st.sregs_[SREG_CS] * 16 + st.ip_;
}

} // unnamed namespace


DebuggerMemState::DebuggerMemState()
    : sr { SREG_INVALID }
    , address { 0, 0, 2 }
{
}

class DebuggerLineParser {
public:
    using LookupResult = std::variant<std::monostate, std::uint64_t, std::function<std::uint64_t(const std::vector<std::uint64_t>&)>>;
    using LookupFunction = std::function<LookupResult (std::string_view)>;

    struct Address {
        std::variant<std::monostate, SReg, uint16_t> segment;
        std::uint64_t offset;

        bool operator==(const Address&) const = default;
    };


    explicit DebuggerLineParser(const std::string& line, LookupFunction lookup = {})
        : line_ { line }
        , lookup_ { lookup }
    {
    }

    bool atEnd() const {
        return pos_ == line_.length();
    }

    std::string_view peekWord()
    {
        size_t end = pos_;
        while (end < line_.length() && !IsSpace(static_cast<uint8_t>(line_[end])))
            ++end;
        return std::string_view { line_.begin() + pos_, line_.begin() + end };
    }

    std::string_view getWord()
    {
        const auto word = peekWord();
        pos_ += word.length();
        return word;
    }

    void skipSpace()
    {
        while (pos_ < line_.length() && IsSpace(static_cast<uint8_t>(line_[pos_])))
            ++pos_;
    }

    std::optional<std::uint64_t> getNumber();

    std::optional<Address> getAddress()
    {
        auto segWord = peekWord();
        if (segWord.empty())
            return {};
        if (segWord.find_first_of(':') == 2)
            segWord = segWord.substr(0, 2);
        else
            segWord = "";
        auto segValue = getNumber();
        if (!segValue)
            return {};

        if (atEnd() || line_[pos_] != ':') {
            return Address { {}, *segValue };
        }

        Address a {};
        if (auto sr = SRegLookup(ToUpperStr(segWord)); static_cast<int>(sr) != SREG_INVALID) {
            a.segment = static_cast<SReg>(sr);
        } else {
            if (*segValue > 0xffff)
                throw std::runtime_error { std::format("Segment 0x{:X} is too large", *segValue) };
            a.segment = static_cast<uint16_t>(*segValue);
        }

        assert(line_[pos_] == ':');
        pos_++;
        auto ofs = getNumber();
        if (!ofs)
            throw std::runtime_error { std::format("Invalid offset") };
        a.offset = *ofs;
        return a;
    }

    char get()
    {
        if (pos_ < line_.length())
            return line_[pos_++];
        throw std::runtime_error { "Out of data in DebuggerLineParser::get()" };
    }

private:
    const std::string& line_;
    LookupFunction lookup_;
    size_t pos_ = 0;
    unsigned nested_ = 0;

    char peek() const
    {
        return pos_ < line_.length() ? line_[pos_] : 0;
    }

    void expect(char ch)
    {
        const auto cur = peek();
        if (cur != ch)
            throw std::runtime_error { std::format("Expected {} got \"{}\"", ch, &line_[pos_]) };
        get();
    }

    void nextToken();
    unsigned parseOperator();
    std::uint64_t parseUnary();
    std::uint64_t parseExpression();
    std::uint64_t parseExpression1(std::uint64_t lhs, unsigned precedence);
    std::uint64_t parseNumberAtom();

    static LookupResult builtinLookup(std::string_view id);
};

std::optional<std::uint64_t> DebuggerLineParser::getNumber()
{
    if (atEnd())
        return {};
    auto val = parseExpression();
    const auto ch = peek();
    if (ch && !IsSpace(ch) && ch != ':')
        throw std::runtime_error { std::format("Unsupported expression \"{}\"", &line_[pos_]) };
    return val;
}

void DebuggerLineParser::nextToken()
{
    if (nested_)
        skipSpace();
}

std::uint64_t DebuggerLineParser::parseUnary()
{
    std::uint64_t number;
    nextToken();
    const auto ch = peek();
    switch (ch) {
    case '~':
        get();
        return ~parseUnary();
    case '+':
        get();
        return parseUnary();
    case '-':
        get();
        return -static_cast<std::int64_t>(parseUnary());
    case '(':
        get();
        ++nested_;
        number = parseExpression();
        --nested_;
        expect(')');
        return number;
    default:
        try {
            return parseNumberAtom();
        } catch (...) {
            if (lookup_ && ch && !IsDigit(ch)) {
                auto start = pos_, end = pos_ + 1;
                while (end < line_.length() && IsIdContChar(line_[end]))
                    ++end;
                pos_ = end;
                std::string_view id { line_.begin() + start, line_.begin() + end };
                auto lookupResult = lookup_(id);
                if (!lookupResult.index())
                    lookupResult = builtinLookup(id);
                if (lookupResult.index()) {
                    if (lookupResult.index() == 1)
                        return std::get<1>(lookupResult);

                    ++nested_;
                    expect('(');
                    std::vector<std::uint64_t> args;
                    for (;;) {
                        nextToken();
                        if (peek() == ')') {
                            get();
                            break;
                        }
                        if (!args.empty()) {
                            expect(',');
                            nextToken();
                        }
                        args.push_back(parseExpression());
                    }
                    --nested_;

                    return std::get<2>(lookupResult)(args);
                }
            }
            throw;
        }
    }
}

std::uint64_t DebuggerLineParser::parseExpression()
{
    return parseExpression1(parseUnary(), MAX_PRECEDENCE);
}

unsigned DebuggerLineParser::parseOperator()
{
    nextToken();
    const auto ch = peek();
    switch (ch) {
    case '+':
    case '-':
    case '*':
    case '/':
    case '%':
    case '&':
    case '^':
    case '|':
        get();
        return ch;
    case '<':
        get();
        if (peek() != '<')
            break;
        get();
        return OP_LSH;
    case '>':
        get();
        if (peek() != '>')
            break;
        get();
        return OP_RSH;
    default:
        return 0;
    }

    throw std::runtime_error {std::format("Unsupported operator {} followed by \"{}\"", ch, &line_[pos_])};
}

std::uint64_t DebuggerLineParser::parseExpression1(std::uint64_t lhs, unsigned outerPrecedence)
{
    for (;;) {
        const auto op = parseOperator();
        const auto precedence = OperatorPrecedence(op);
        if (precedence > outerPrecedence)
            return lhs;
        auto rhs = parseUnary();
        for (;;) {
            const auto savedPos = pos_;
            const auto rhsOp = parseOperator();
            const auto rhsPrec = OperatorPrecedence(rhsOp);
            pos_ = savedPos;
            if (rhsPrec > precedence) // TODO: RTL
                break;
            rhs = parseExpression1(rhs, rhsPrec);
        }
        switch (op) {
        case '+':
            lhs += rhs;
            break;
        case '-':
            lhs -= rhs;
            break;
        case '*':
            lhs *= rhs;
            break;
        case '/':
            if (!rhs)
                throw std::runtime_error { "Division by zero" };
            lhs /= rhs;
            break;
        case '%':
            if (!rhs)
                throw std::runtime_error { "Division by zero" };
            lhs %= rhs;
            break;
        case OP_LSH:
            lhs <<= rhs;
            break;
        case OP_RSH:
            lhs >>= rhs;
            break;
        case '&':
            lhs &= rhs;
            break;
        case '^':
            lhs ^= rhs;
            break;
        case '|':
            lhs |= rhs;
            break;
        default:
            throw std::runtime_error { std::format("TODO in parseExpression1 handle: {} {} {}", lhs, OperatorString(op), rhs) };
        }
    }
}

std::uint64_t DebuggerLineParser::parseNumberAtom()
{
    size_t end = pos_;
    while (end < line_.length() && (IsNumberSeparator(line_[end]) || IsAlnum(line_[end])))
        ++end;
    std::string_view s { line_.begin() + pos_, line_.begin() + end };
    if (s.empty())
        throw std::runtime_error { std::format("Number expected got: \"{}\"", &line_[pos_]) };

    const auto origS = s;

    std::uint64_t number = 0;
    unsigned base = 16;
    if (IsDigit(s.front()) && ToUpper(s.back()) == 'H') {
        s = s.substr(0, s.length() - 1);
    } else if (s[0] == '0' && s.length() > 2) {
        switch (ToUpper(s[1])) {
        case 'B':
            base = 2;
            s = s.substr(2);
            break;
        case 'N':
            base = 10;
            s = s.substr(2);
            break;
        case 'X':
            base = 16;
            s = s.substr(2);
            break;
        }
    }

    bool anyDigit = false;
    for (size_t i = 0; i < s.length(); ++i) {
        if (IsNumberSeparator(s[i]))
            continue;
        const auto val = DigitValue(s[i]);
        if (val > base)
            throw std::runtime_error { std::format("\"{}\" is not a valid number (invalid base {} digit)", origS, base) };
        const auto nextNumber = number * base + val;
        if (nextNumber < number)
            throw std::runtime_error { std::format("\"{}\" is too large", origS) };
        number = nextNumber;
        anyDigit = true;
    }

    if (!anyDigit)
        throw std::runtime_error { std::format("\"{}\" is not a valid number (no digits)", origS) };

    pos_ = end;
    return number;
}

DebuggerLineParser::LookupResult DebuggerLineParser::builtinLookup(std::string_view id)
{
    auto func1 = [&id](auto f) {
        return [=](const std::vector<uint64_t>& args) {
            if (args.size() != 1)
                throw std::runtime_error { "Wrong number of arguments for " + std::string(id) };
            return static_cast<uint64_t>(f(args[0]));
        };
    };

    if (id == "s8")
        return func1([](std::uint64_t n) { return static_cast<std::int64_t>(static_cast<std::int8_t>(n & 0xff)); });
    else if (id == "s16")
        return func1([](std::uint64_t n) { return static_cast<std::int64_t>(static_cast<std::int16_t>(n & 0xffff)); });
    else if (id == "s32")
        return func1([](std::uint64_t n) { return static_cast<std::int64_t>(static_cast<std::int32_t>(n & 0xffffffff)); });

    return {};
}

template <>
struct std::formatter<DebuggerLineParser::Address> : std::formatter<const char*> {
    auto format(const DebuggerLineParser::Address& a, std::format_context& ctx) const
    {
        std::string s;
        switch (a.segment.index()) {
        case 0:
            break;
        case 1:
            s = std::string(SRegText[std::get<1>(a.segment)]) + ":";
            break;
        case 2:
            s = std::format("{:04X}:", std::get<2>(a.segment));
            break;
        }
        s += std::format("{:X}", a.offset);
        return std::formatter<const char*>::format(s.c_str(), ctx);
    }
};


template <>
struct std::formatter<DebuggerMemState> : std::formatter<const char*> {
    auto format(const DebuggerMemState& ms, std::format_context& ctx) const
    {
        std::string s;
        if (ms.sr != SREG_INVALID)
            s = std::format("{} ", SRegText[ms.sr]);
        s += std::format("{}", ms.address);
        return std::formatter<const char*>::format(s.c_str(), ctx);
    }
};

Debugger::Debugger(CPU& cpu, SystemBus& bus)
    : cpu_ { cpu }
    , bus_ { bus }
{
    InstallBreakHandler();
}

void Debugger::initMemState(DebuggerMemState& ms, SReg sr, uint64_t offset)
{
    assert(static_cast<uint32_t>(sr) <= 6);
    ms.sr = sr;
    ms.address = Address { cpu_.sregs_[sr], offset, static_cast<uint8_t>(cpu_.protectedMode() ? 4 : 2) };
}

uint64_t Debugger::toPhys(uint64_t linearAddress)
{
    if (!cpu_.pagingEnabled())
        return linearAddress;
    const auto pde = peekMem(cpu_.cregs_[3] + (linearAddress >> 22) * 4, 4);
    if (!(pde & PT32_MASK_P))
        throw std::runtime_error { std::format("{:08X} not present in PD", linearAddress) };
    const auto pte = peekMem((pde & PT32_MASK_ADDR) + ((linearAddress >> 12) & 1023) * 4, 4);
    if (!(pte & PT32_MASK_P))
        throw std::runtime_error { std::format("{:08X} not present in PT", linearAddress) };
    return (pte & PT32_MASK_P) + (linearAddress & PAGE_MASK);
}

uint64_t Debugger::toPhys(const DebuggerMemState& ms, uint64_t offset)
{
    const auto& a = ms.address;
    offset += a.offset();
    if (ms.sr != SREG_INVALID)
        return toPhys(cpu_.sdesc_[ms.sr].base + offset);

    if (cpu_.protectedMode()) {
        std::println("WARNING: Protected mode enabled and sr is invalid!");
    }

    return toPhys(a.segment() * 16 + offset);
}

void Debugger::activate()
{
    if (!active_) {
        active_ = true;
        traceCount_ = 0;
        autoBreakPoint_.active = false;
        initMemState(disAsmAddr_, SREG_CS, cpu_.ip_);
        if (onSetActive_)
            onSetActive_(true);
    }
}

bool Debugger::checkBreakPoint(const BreakPoint& bp)
{
    if (!bp.active)
        return false;
    assert(!cpu_.protectedMode()); // TODO
    if (GetPhysicalIp(cpu_) != bp.phys)
        return false;
    activate();
    return true;
}

void Debugger::check(void)
{
    if (breakActivated) {
        InstallBreakHandler();
        activate();
    }
    if (traceCount_) {
        if (--traceCount_ == 0)
            activate();
        else
            cpu_.trace();
    }
    checkBreakPoint(autoBreakPoint_);
    for (size_t i = 0; i < maxBreakPoints; ++i) {
        if (checkBreakPoint(breakPoints_[i]))
            std::println("Breakpoint {} hit", i);
    }
    if (active_)
        commandLoop();
}

void Debugger::commandLoop()
{
    std::string line;
    cpu_.trace();
    for (;;) {
        std::cout << "> " << std::flush;
        if (!std::getline(std::cin, line)) {
            std::println("getline failed");
            exit(0);
        }
        line = TrimString(line);
        if (line.empty())
            continue;
        try {
            if (!handleLine(line))
                break;
        } catch (const std::exception& e) {
            std::println("{}", e.what());
        }
    }

    active_ = false;
    if (onSetActive_)
        onSetActive_(false);
}

void Debugger::addBreakPoint(std::uint64_t physicalAddress)
{
    for (size_t i = 0; i < maxBreakPoints; ++i) {
        auto& bp = breakPoints_[i];
        if (bp.active)
            continue;
        bp.active = true;
        bp.phys = physicalAddress;
        std::println("Breakpoint {} added: {:X}", i, physicalAddress);
        return;
    }
    throw std::runtime_error { "Too many breakpoints" };
}

uint64_t Debugger::peekMem(uint64_t physAddress, size_t size)
{
    assert(size <= 8);
    uint64_t value = 0;
    for (size_t i = size; i--;)
        value = value << 8 | bus_.peekU8(physAddress + i);
    return value;
}

bool Debugger::handleLine(const std::string& line)
{
    assert(!line.empty());

    auto readMemFunc = [this](size_t size) {
        return [this, size](const std::vector<uint64_t>& args) -> std::uint64_t {
            if (args.size() != 1)
                throw std::runtime_error { std::format("Invalid number of arguments for m{}", 8 << size) };
            return peekMem(args[0], size);
        };
    };

    DebuggerLineParser::LookupFunction lookupFunc = [&](std::string_view id) -> DebuggerLineParser::LookupResult {
        if (auto reg = RegLookup(cpu_, id); reg.first)
            return RegGet(reg);
        else if (id == "m8")
            return readMemFunc(1);
        else if (id == "m16")
            return readMemFunc(2);
        else if (id == "m32")
            return readMemFunc(4);
        else if (id == "m64")
            return readMemFunc(8);
        return {};
    };

    DebuggerLineParser parser { line, lookupFunc };
    if (line[0] == '?') {
        parser.get();
        for (;;) {
            parser.skipSpace();
            auto n = parser.getNumber();
            if (!n)
                break;
            std::println("{:08X}`{:08X} 0b{:08b}`{:08b}`{:08b}`{:08b}`{:08b}`{:08b}`{:08b}`{:08b} {} {}", *n >> 32, *n & 0xffffffff, (*n >> 56) & 0xff, (*n >> 48) & 0xff, (*n >> 40) & 0xff, (*n >> 32) & 0xff, (*n >> 24) & 0xff, (*n >> 16) & 0xff, (*n >> 8) & 0xff, *n & 0xff, *n, static_cast<int64_t>(*n));
        }
        return true;
    }
    const auto cmd = parser.getWord();
    assert(!cmd.empty());
    parser.skipSpace();

    auto getAddress = [&](DebuggerMemState& memState) {
        if (auto addr = parser.getAddress(); addr) {
            switch (addr->segment.index()) {
            case 0:
                memState.address = Address { memState.address.segment(), addr->offset, memState.address.offsetSize() };
                break;
            case 1:
                initMemState(memState, std::get<1>(addr->segment), addr->offset);
                break;
            case 2:
                memState.sr = SREG_INVALID;
                memState.address = Address { std::get<2>(addr->segment), addr->offset, memState.address.offsetSize() };
                break;
            }
            parser.skipSpace();
            return true;
        }
        return false;
    };

    constexpr uint64_t defaultNumLines = 10;
    auto getLines = [&]() {
        if (auto nl = parser.getNumber(); nl) {
            if (*nl < 1000)
                return *nl;
            else
                std::print("Too many lines {}\n", *nl);
        }
        return defaultNumLines;
    };

    auto getAddressAndNumLines = [&](DebuggerMemState& memState) {
        uint64_t numLines = defaultNumLines;
        if (getAddress(memState))
            numLines = getLines();
        return numLines;
    };

    if (cmd == "b") {
        for (size_t i = 0; i < maxBreakPoints; ++i) {
            auto& bp = breakPoints_[i];
            if (bp.active)
                std::println("0x{:X} {:X}", i, bp.phys);
        }
    } else if (cmd == "bd") {
        auto index = parser.getNumber();
        if (!index || *index >= maxBreakPoints)
            throw std::runtime_error { "Invalid breakpoint index" };
        breakPoints_[*index].active = false;
    } else if (cmd == "bp") {
        auto phys = parser.getNumber();
        if (!phys)
            throw std::runtime_error { "Physical address missing" };
        addBreakPoint(*phys);
    } else if (cmd == "d" || cmd == "dp" || cmd == "d16" || cmd == "d32" || cmd == "dp16" || cmd == "dp32") {
        const bool isPhys = cmd.length() > 1 && cmd[1] == 'p';
        auto cpuInfo = cpu_.cpuInfo();
        if (cmd.ends_with("16"))
            cpuInfo.defaultOperandSize = 2;
        else if (cmd.ends_with("32"))
            cpuInfo.defaultOperandSize = 4;

        uint64_t numLines = defaultNumLines;
        if (isPhys) {
            if (auto physAddr = parser.getNumber(); physAddr) {
                disAsmAddr_.sr = SREG_INVALID;
                disAsmAddr_.address = Address { 0, *physAddr, cpu_.defaultOperandSize() };
                numLines = getLines();
            }
        } else {
            numLines = getAddressAndNumLines(disAsmAddr_);
        }
        uint64_t offset;
        auto ifetch = [&]() {
            uint64_t addr = isPhys ? disAsmAddr_.address.offset() : toPhys(disAsmAddr_, offset);
            offset++;
            return bus_.peekU8(addr);
        };

        auto& addr = disAsmAddr_.address;
        while (numLines--) {
            offset = 0;
            try {
                auto res = Decode(cpuInfo, ifetch);
                std::print("{}\n", FormatDecodedInstructionFull(res, addr));
                addr += res.numInstructionBytes;
            } catch (const std::exception& e) {
                std::print("{} - {}\n", addr, e.what());
                break;
            }
        }
    } else if (cmd == "g") {
        return false;
    } else if (cmd == "gdt") {
        const auto& gdt = cpu_.gdt_;
        std::println("GDT base={:08X} limit={:04X}", gdt.base, gdt.limit);
        for (uint32_t offset = 0; offset + 7 <= gdt.limit; offset += 8) {
            const auto descValue = peekMem(toPhys(gdt.base + offset), 8);
            if (descValue & DESCRIPTOR_MASK_PRESENT) {
                const auto desc = SegmentDescriptor::fromU64(descValue);
                std::println("{:02X} {:016X} {}", offset, descValue, desc);
            }
        }
    } else if (cmd == "h") {
        cpu_.showHistory();
    } else if (cmd == "hc") {
        cpu_.showControlTransferHistory();
    } else if (cmd == "idt") {
        const auto& idt = cpu_.idt_;
        std::println("IDT base={:08X} limit={:04X}", idt.base, idt.limit);
        for (uint32_t idtOffset = 0; idtOffset + 7 <= idt.limit; idtOffset += 8) {
            const auto desc = peekMem(toPhys(idt.base + idtOffset), 8);
            if (desc) {
                const auto offset = (desc & 0xffff) | ((desc >> 48) << 16);
                const auto selector = static_cast<uint16_t>((desc >> 16) & 0xffff);
                const auto flags = (desc >> 40) & 0xff;
                const auto type = flags & 0xf;
                const auto dpl = (flags >> 5) & 3;
                std::println("Int{:02X} {:016X} {:X}:{:08X} DPL={} Type={:02X}", idtOffset / 8, desc, selector, offset, dpl, type);
            }
        }
    } else if (cmd == "m") {
        uint64_t numLines = getAddressAndNumLines(hexDumpAddr_);
        HexDump(hexDumpAddr_.address, numLines * 16, [&](uint64_t offset) -> std::optional<std::uint8_t> {
            try {
                return bus_.peekU8(toPhys(hexDumpAddr_, offset));
            } catch (...) {
                return {};
            }
        });
        hexDumpAddr_.address += numLines * 16;
    } else if (cmd == "phys") {
        DebuggerMemState ms;
        initMemState(ms, SREG_CS, 0);
        if (getAddress(ms))
            std::println("{} - {:08X}", ms, toPhys(ms, 0));
    } else if (cmd == "r") {
        if (auto regName = parser.getWord(); !regName.empty()) {
            if (auto regInfo = RegLookup(cpu_, regName); regInfo.first) {
                parser.skipSpace();
                auto value = parser.getNumber();
                if (!value)
                    throw std::runtime_error { std::format("Value expected for {}", regName) };
                RegSet(regInfo, *value);
                // For safety's sake clear the prefetch buffer
                // TODO: Handle changing of sregs in protected mode
                std::println("Clearing prefetch buffer");
                cpu_.prefetch_.flush(cpu_.ip_ & cpu_.ipMask());
            } else {
                throw std::runtime_error { std::format("Invalid register {}", regName) };
            }
        }
        ShowCPUState(cpu_);
    } else if (cmd == "search") {
        auto bytes = HexDecode(parser.getWord());
        if (bytes.empty())
            throw std::runtime_error { "Missing argument" };

        const uint64_t start = 0;
        const uint64_t end = 1 << 20;
        for (uint64_t pos = start, sz = bytes.size(); pos + sz <= end; ++pos) {
            if (bus_.peekU8(pos) != bytes[0])
                continue;
            bool found = true;
            for (uint64_t j = 1; j < bytes.size() && found; ++j)
                found &= bus_.peekU8(pos + j) == bytes[j];
            if (found)
                std::println("Found at {:X}", pos);
        }
    } else if (cmd == "sr") {
        for (int i = 0; i < 6; ++i) {
            std::println("{} {:04X} {}", SRegText[i], cpu_.sregs_[i], cpu_.sdesc_[i]);
        }
    } else if (cmd == "t") {
        traceCount_ = 1;
        if (auto n = parser.getNumber(); n) {
            if (*n >= 100000)
                throw std::runtime_error { std::format("{} is too large for trace count", *n) };
            traceCount_ = static_cast<uint32_t>(*n);
        }
        return false;
    } else if (cmd == "q") {
        exit(0);
    } else if (cmd == "z") {
        auto phys = GetPhysicalIp(cpu_);
        (void)Decode(cpu_.cpuInfo(), [&]() { return bus_.peekU8(phys++); });
        autoBreakPoint_.active = true;
        autoBreakPoint_.phys = phys;
        return false;
    } else {
        std::print("Unknown command \"{}\"\n", cmd);
    }

    return true;
}

void TestDebugger()
{
    const struct {
        const char* text;
        std::uint64_t number;
    } expressionTests[] = {
        { "0", 0 },
        { "7", 7 },
        { "42", 0x42 },
        { "2a", 42 },
        { "0c", 12 },
        { "0n42", 42 },
        { "0n9", 9 },
        { "0X12", 0x12 },
        { "0b101010", 42 },
        { "0abcdh", 0xabcd },
        { "0CD12H", 0xcd12 },
        { "123h", 0x123 },
        { "1234`5678", 0x12345678 },
        { "+2a", 42 },
        { "-2", (uint64_t)(int64_t)-2 },
        { "~1234", ~(uint64_t)0x1234 },
        { "2+3", 5 },
        { "2a+3", 45 },
        { "20-5", 32 - 5 },
        { "2+3*4", 2 + 3 * 4 },
        { "1+2+3", 6 },
        { "(1+2)*4", 12 },
        { "4*5+2", 22 },
        { "22 +3", 0x22 }, // Whitespace terminates expression
        { "(  \t 1 + 2 +      3   )", 6 }, // But not inside parenthesis
        { "0n123/0n10", 12 },
        { "0n123%0n10", 3 },
        { "16^4", 0x12 },
        { "10|20", 0x30 },
        { "abc&1004", 4 },
        { "-1&ffff", 0xffff },
        { "abc<<4", 0xabc0 },
        { "abc>>7", 0x15 },
        { "ax", 0x1234 },
        { "ax+2", 0x1236 },
        { "not(42)", ~0x42ULL },
        { "xadd(    1,   2 \t  )", 3 }, // "add" would be interpreted as a number..
        { "not( xadd(1,xadd(2,3)) )", ~6ULL },
        // builtin functions
        { "s8(10ff)", (uint64_t)-1 },
        { "s16(10ffff)", (uint64_t)-1 },
        { "s32(41ffff0000)", (uint64_t)-65536 },
    };

    const struct {
        const char* text;
        DebuggerLineParser::Address addr;
    } addressTests[] = {
        { "aBcD:5678", { uint16_t(0xabcd), 0x5678 } },
        { "42", { {}, 0x42 } },
        { "ax:2+3", { uint16_t(0x1234), 5 } },
        { "cs:1234", { SREG_CS, 0x1234 } },
        { "ds:0", { SREG_DS, 0 } },
        { "es:0", { SREG_ES, 0 } },
        { "ss:0", { SREG_SS, 0 } },
        { "fs:0", { SREG_FS, 0 } },
        { "gs:12345678", { SREG_GS, 0x12345678 } },
        { "fs", { {}, 0xf5f5 } },
        { "fs+2:2a", { uint16_t(0xf5f5+2), 42 } },
        { "CS:0", { SREG_CS, 0 } },
        { "DS:0", { SREG_DS, 0 } },
        { "eS:0", { SREG_ES, 0 } },
        { "Ss:0", { SREG_SS, 0 } },
        { "FS:0", { SREG_FS, 0 } },
        { "GS:0", { SREG_GS, 0 } },
    };

    DebuggerLineParser::LookupFunction lookupFunc = [](std::string_view id) -> DebuggerLineParser::LookupResult {
        if (id == "ax")
            return 0x1234ULL;
        if (id == "fs")
            return 0xf5f5ULL;
        if (SRegLookup(ToUpperStr(id)) != SREG_INVALID)
            return 0xcdcdULL;
        if (id == "not")
            return [](const std::vector<std::uint64_t>& args) {
                if (args.size() != 1)
                    throw std::runtime_error { "Wrong args for not" };
                return ~args[0];
            };
        if (id == "xadd")
            return [](const std::vector<std::uint64_t>& args) {
                if (args.size() != 2)
                    throw std::runtime_error { "Wrong args for xadd" };
                return args[0] + args[1];
            };
        return {};
    };

    for (const auto& [text, number] : expressionTests) {
        try {
            const std::string line { text };
            DebuggerLineParser lp { line, lookupFunc };
            const auto n = lp.getNumber();
            if (!n)
                throw std::runtime_error { "No number returned" };
            if (*n != number)
                throw std::runtime_error { std::format("Got {} 0x{:X} expected {} 0x{:X}", *n, *n, number, number) };                
        } catch (const std::exception& e) {
            std::cout << "Test failed for " << text << ": " << e.what() << "\n";
            exit(1);
        }
    }

    for (const auto& [text, expected] : addressTests) {
        try {
            const std::string line { text };
            DebuggerLineParser lp { line, lookupFunc };
            auto addr = lp.getAddress();
            if (!addr)
                throw std::runtime_error { "No address returned" };
            if (*addr != expected)
                throw std::runtime_error { std::format("Got {} expected {}", *addr, expected) };
        } catch (const std::exception& e) {
            std::cout << "Test failed for " << text << ": " << e.what() << "\n";
            exit(1);
        }
    }
}
