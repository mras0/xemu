import xml.etree.ElementTree as ET

instructions = set()
modes = set()

tab1 = []
tab2 = []
has_modrm1 = []
has_modrm2 = []
cpu_model = 0

reg8  = ['AL', 'CL', 'DL', 'BL', 'AH', 'CH', 'DH', 'BH']
reg32 = ['eAX', 'eCX', 'eDX', 'eBX', 'eSP', 'eBP', 'eSI', 'eDI']
sreg  = ['ES', 'CS', 'SS', 'DS', 'FS', 'GS']

OPTABLE = "opcode_types"
INDENT="    "
OMODE="OperandMode"
INS="InstructionMnem"

def reset():
    global tab1
    global tab2
    global has_modrm1
    global has_modrm2
    tab1 = [None] * 256
    tab2 = [None] * 256
    has_modrm1 = [0] * (256//32)
    has_modrm2 = [0] * (256//32)

def set_modrm(tab, opcode):
    assert opcode >= 0 and opcode <= 255
    if tab is tab1:
        has_modrm1[opcode // 32] |= 1 << (opcode % 32)
    else:
        assert tab is tab2
        has_modrm2[opcode // 32] |= 1 << (opcode % 32)

def add_ins(tab, opcode, mnemonic, operands, ext):

    oname = ("" if tab is tab1 else "0F") + "%02X" % opcode

    for o in operands:
        modes.add(o)
    instructions.add(mnemonic)

    if (v := tab[opcode]) is not None:
        if ((ext is None) and v is not None) or ((ext is not None) and not isinstance(v, list)):
                raise Exception('Opcode extension mismatch for %s %s' % (oname, mnemonic))

    info = {"mnemonic": mnemonic, "operands": list(operands)}

    for o in operands:
        if o in reg8 or o in reg32 or o in sreg or o in ["C1", "DX"]:
            continue
        a = o[0]
        if a in "AIJO":
            continue
        modrm = False
        if a in "CDEGRSTVW":
            modrm = True
        elif o == "Mtpt":
            pass
        elif a == "M":
            modrm = True
        else:
            raise Exception('TODO: Check if "%s" has modr/m byte for %s %s' % (o, oname, mnemonic))
        if modrm:
            set_modrm(tab, opcode)

    if ext is None:
        tab[opcode] = info
        return

    assert ext >= 0 and ext <= 7

    if tab[opcode] is None:
        set_modrm(tab, opcode)
        tab[opcode] = [None] * 8

    assert tab[opcode][ext] is None
    tab[opcode][ext] = info

def handle_op(op):
    if op.text:
        if 'displayed' in op.attrib and op.attrib['displayed'] == "no":
            return None
        # Filter operands for MOVS/XLAT etc.
        a = op.attrib['address'] if 'address' in op.attrib else None
        if a == "X" or a == "Y" or a == "BB":
            return None
        if a == "I":
            if op.text == "1":
                return "C1"
            else:
                raise Exception('Unknown immediate %s' % op.text)
        mod = op.text
    else:
        mod = op.find('a').text
        t = op.find('t')
        if t is not None:
            t = t.text
            if t == "vqp": # Ignore 64-bit
                t = "v"
            mod += t
    if mod == "rAX":
        mod = "eAX"
    return mod

def is_string_op(op):
    a = op.attrib.get('address', '')
    return a == "X" or a == "Y"

def output_entry(tab, opcode, e):

    syntax = e.find('syntax')
    mnem = syntax.find('mnem')
    if mnem is None:
        return

    mnem = mnem.text
    if mnem == "NOP":
        return
    # Seems wrong 3+'s for e.g. 83/1
    if not (grp1 := e.find('grp1')) is None:
        if grp1.text == 'x87fpu':
            return
    if (sec_opcd := e.find('sec_opcd')) is not None:
        # E.g. AAM
        #print("%02X %s second operand: %s" % (opcode, mnem, sec_opcd.text))
        return

    ops = [d for d in syntax.iter('dst')] + [s for s in syntax.iter('src')]
    if any(map(is_string_op, ops)):
        # Hack for string OPS (MOVSB etc.)
        if (opcode & 1) == 0:
            mnem += "B"

    ops = list(filter(lambda x: x is not None, map(handle_op, ops)))

    ext = e.find('opcd_ext')
    if ext is not None:
        ext = int(ext.text)

    if mnem in ["PUSHAD", "POPAD", "PUSHFD", "POPFD", "IRETD"]:
        mnem = mnem[0:-1]

    if 'mod' in syntax.attrib and syntax.attrib['mod'] == 'mem':
        assert len(ops) > 0
        # e.g. SMSW is Mw/r16/32/64
        if ops[0] == 'Mw':
            ops[0] = "MwRv"
        elif ops[1] == 'Mw':
            # LAR/LSL
            ops[1] = "MwRv"

    if len(ops) > 0 and ops[0][0] == "Z":
        # Z: The instruction has no ModR/M byte; the three least-significant bits of the 
        #    opcode byte selects a general-purpose register
        if ops[0] == "Zb":
            regs = reg8
        elif ops[0] == "Zv": # v=16/32
            regs = reg32 # 16/32-bit
        else:
            raise Exception('Unknown Z-mode %s' % ops[0])
        for i in range(8):
            ops[0] = regs[i]
            add_ins(tab, opcode + i, mnem, ops, ext)
    else:
        add_ins(tab, opcode, mnem, ops, ext)

    #print('\t%s\t%s%s' % (mnem.text, ", ".join(ops), ext))

def write_cpu_tables():
    CPU= "8086" if cpu_model == 0 else "80%d86" % cpu_model
    with open("opcodes_%s.cpp" % CPU, 'wt') as f:
        def handle_ins(ins, comment):
            f.write(INDENT + "{ %s::" % INS)
            if ins is None:
                f.write("UNDEF")
            else:
                f.write("%s" % (ins['mnemonic']))
                ops = ins['operands']
                if len(ops):
                    f.write(", { %s }" % (", ".join(map(lambda o: "%s::%s" % (OMODE, o), ops))))
            f.write(" },")
            if comment is not None:
                f.write(" // %s" % comment)
            f.write("\n")

        f.write('#include "opcodes.h"\n\n')

        TABPREFIX = "ITable_"
        for opcode in range(256):
            lst = tab1[opcode]
            if not isinstance(lst, list):
                continue
            f.write("static const Instruction %s%02X[8] = {\n" % (TABPREFIX, opcode))
            for i in range(8):
                handle_ins(lst[i], "%02X/%d" % (opcode, i))
            f.write("};\n\n")

        f.write("const Instruction InstructionTable_%s[256] = {\n" % CPU)
        for opcode in range(256):
            ins = tab1[opcode]
            if isinstance(ins, list):
                f.write(INDENT + "{ .mnemonic = %s::TABLE, .table = %s%02X }, // %02X\n" % (INS, TABPREFIX, opcode, opcode))
            else:
                handle_ins(ins, "%02X" % opcode)
        f.write("};\n\n")

        if cpu_model >= 2:
            for opcode in range(256):
                lst = tab2[opcode]
                if not isinstance(lst, list):
                    continue
                f.write("static const Instruction %s0F_%02X[8] = {\n" % (TABPREFIX, opcode))
                for i in range(8):
                    handle_ins(lst[i], "%02X/%d" % (opcode, i))
                f.write("};\n\n")

            f.write("const Instruction InstructionTable_0F_%s[256] = {\n" % CPU)
            for opcode in range(256):
                ins = tab2[opcode]
                if isinstance(ins, list):
                    f.write(INDENT + "{ .mnemonic = %s::TABLE, .table = %s0F_%02X }, // 0F %02X\n" % (INS, TABPREFIX, opcode, opcode))
                else:
                    handle_ins(ins, "%02X" % opcode)
            f.write("};\n\n")

        def write_modrm(idx):
            f.write("const uint32_t HasModrm%d_%s[256/32] = {\n" % (idx, CPU))
            if idx == 1:
                tab = has_modrm1
            else:
                assert idx == 2
                tab = has_modrm2
            for i in range(256//32):
                if i % 8 == 0:
                    f.write(INDENT)
                f.write("0x%08X," % tab[i])
                if i % 8 == 7:
                    f.write("\n")
                else:
                    f.write(" ")
            f.write("};\n")

        write_modrm(1)
        if cpu_model >= 2:
            f.write('\n')
            write_modrm(2)


tree = ET.parse('misc/x86reference/x86reference.xml')
root = tree.getroot()
assert root.tag == 'x86reference'

def entry_cpu(e):
    if not (proc_start := e.find('proc_start')) is None:
        return int(proc_start.text)
    else:
        return 0

def handle_entries(tab, op, opbyte):
    # C0/C1 have proc_start inside pri_opcd (rather than for each entry)
    if entry_cpu(op) > cpu_model:
        return

    entries = []
    max_cpu = 0

    for e in op.iter('entry'):
        if e.attrib.get('mode','') == 'e': # 64-bit mode
            continue
        cpu = entry_cpu(e)
        if opbyte == 0x83: # Seems like 0x83 is classfied wrongly
            cpu = 0
        if cpu > cpu_model:
            #print('--Skipping %d only instruction %02X %s --' % (cpu*100+86, opcode, mnem))
            continue
        if cpu > max_cpu:
            max_cpu = cpu
        entries.append(e)

    for e in entries:
        if max_cpu > 0 and entry_cpu(e) < max_cpu:
            #print("XXX: Special handling for %02X %d < %d" % (opbyte, entry_cpu(e), max_cpu))
            continue
        output_entry(tab, opbyte, e)

        # E.g. 0F20
        if e.attrib.get('r','') == "yes":
            break

def handle_cpu(model):
    global cpu_model
    cpu_model = model
    reset()

    # INT3 + prefixes
    prefixes = [0x26, 0x2E, 0x36, 0x3E, 0xF2, 0xF3]
    if cpu_model >= 3:
        prefixes += [0x64, 0x65, 0x66, 0x67, 0xF0 ]
    skipped = set(prefixes + [0x8C, 0xCC])

    add_ins(tab1, 0x8C, "MOV", ["Ew", "Sw"], None)
    add_ins(tab1, 0xCC, "INT3", [], None)

    for p in prefixes:
        tab1[p] = {"mnemonic": "PREFIX", "operands" : []}

    # ESCape (FPU)
    instructions.add("ESC")
    for op in range(0xD8, 0xE0):
        tab1[op] = 8 * [{"mnemonic": "ESC", "operands": [ "Eb" ]}]
        set_modrm(tab1, op)

    for op in root.find('one-byte'):
        assert op.tag == 'pri_opcd'
        opbyte = int(op.attrib['value'], 16)
        if opbyte in skipped:
            continue
        handle_entries(tab1, op, opbyte)

    # Override NOP
    tab1[0x90] = {"mnemonic": "NOP", "operands": []}
    instructions.add("NOP")

    # SALC is always available
    tab1[0xD6] = {"mnemonic": "SALC", "operands": []}

    for op in root.find('two-byte'):
        assert op.tag == 'pri_opcd'
        opbyte = int(op.attrib['value'], 16)
        handle_entries(tab2, op, opbyte)

    if model >= 3:
        # Handle MOV from Sreg. Word-sized for memory, but can be to a 32-bit register
        tab1[0x8C]["operands"][0] = "MwRv"
        modes.add("MwRv")

        add_ins(tab1, 0x9B, "FWAIT", [], None)

    #
    # Handle partial decoding
    #
    def copy_ins(tab, dst, src):
        assert tab[dst] is None
        assert tab[src] is not None
        tab[dst] = tab[src]
    def fill_table(lst):
        assert isinstance(lst, list)
        for i in range(1,8):
            lst[i] = lst[0]

    if model == 0:
        # https://www.righto.com/2023/07/undocumented-8086-instructions.html
        # 6x = 7x (Jcc)
        for i in range(16):
            copy_ins(tab1, 0x60|i, 0x70|i)

        # C6/C7 just ignore /r for r > 0
        fill_table(tab1[0xC6])
        fill_table(tab1[0xC7])

        # C0/C1 ignore lsb
        def ignore_bit1(ins):
            assert ins & 2 == 0
            copy_ins(tab1, ins, ins|2)
        ignore_bit1(0xC0)
        ignore_bit1(0xC1)
        ignore_bit1(0xC8)
        ignore_bit1(0xC9)

        # D0..D3/6 SETMO
        instructions.add("SETMO")
        for i in range(0xD0, 0xD4):
            assert isinstance(tab1[i], list)
            assert tab1[i][6]["mnemonic"] == "SAL"
            tab1[i][6]["mnemonic"] = "SETMO"

        # FF/7 = FF/6
        assert tab1[0xFF][0x07] is None
        tab1[0xFF][0x07] = tab1[0xFF][0x06]

    if model > 0:
        # INS/OUTS
        assert tab1[0x6C]["operands"] == ["DX"]
        assert tab1[0x6D]["operands"] == ["DX"]
        assert tab1[0x6E]["operands"] == ["DX"]
        assert tab1[0x6F]["operands"] == ["DX"]
        tab1[0x6C]["operands"] = ""
        tab1[0x6D]["operands"] = ""
        tab1[0x6E]["operands"] = ""
        tab1[0x6F]["operands"] = ""

    if model == 3:
        # 0F/9x SETcc
        for i in range(0x90, 0xA0):
            fill_table(tab2[i])


    write_cpu_tables()

handle_cpu(0)
handle_cpu(3)

modes = sorted(modes)
instructions = ["UNDEF", "TABLE", "PREFIX"] + sorted(instructions)

mtext=["None", "1", "DX"]

with open(OPTABLE + ".h", 'wt') as f:
    guard = OPTABLE.upper() + "_H"
    f.write("#ifndef %s\n#define %s\n\n" % (guard, guard))
    f.write("#include <stdint.h>\n\n")

    f.write("enum class %s {\n" % OMODE)

    f.write(INDENT + "None,\n")
    f.write(INDENT + "C1, // Constant 1\n")
    f.write(INDENT + "DX, // Always DX\n")

    def rlist(rlst):
        for r in rlst:
            f.write(INDENT + "%s,\n" % r)
            mtext.append(r)
    f.write(INDENT + "// 8-bit registers\n")
    rlist(reg8)
    f.write(INDENT + "// 16/32-bit registers\n")
    rlist(reg32)
    f.write(INDENT + "// Segment registers\n")
    rlist(sreg)
    f.write(INDENT + "// Remaining modes\n")
    for m in modes:
        if m == "C1" or m == "DX" or m in reg8 or m in reg32 or m in sreg:
            continue
        f.write(INDENT + "%s,\n" % m)
        mtext.append(m)
    f.write("};\n\n")

    f.write("enum class %s {\n" % INS)
    for i in instructions:
        f.write(INDENT + "%s,\n" % i)
    f.write("};\n")

    f.write("""

static constexpr int MaxInstructionOperands = 3;

struct Instruction {
    InstructionMnem mnemonic;
    union {
        OperandMode operands[MaxInstructionOperands];
        const Instruction* table;
    };
};

extern const char* const ModeStrings[];
extern const char* const MnemonicStrings[];
""")

    f.write("\n#endif\n")

#import sys
#f = sys.stdout

with open(OPTABLE + ".cpp", 'wt') as f:
    f.write('#include "%s.h"\n\n' % OPTABLE)
    f.write('const char* const ModeStrings[] = {\n')
    for m in mtext:
        f.write(INDENT + '"%s",\n' % m);
    f.write('};\n\n');

    f.write('const char* const MnemonicStrings[] = {\n')
    for i in instructions:
        f.write(INDENT + '"%s",\n' % i);
    f.write('};\n\n');
