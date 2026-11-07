"""Compare what core/own_sage_payload.h decodes from Switch 2 instructions with NVIDIA's disassembler.

    py -3.11 tests/own_sage_nvdisasm.py <nvdisasm text> <fields file>

The inputs come from `own_sage_tool stream <archive> <out> [every]` (the instruction stream and <out>.fields) and
`nvdisasm -b SM86 <out>` run on the stream (CUDA 13.1 on the desktop). Both are private: game code. For every
instruction the guard predicate is compared; for the opcodes listed below also the destination register, the
constant-bank operand and the immediate. Prints the counts and the first disagreements.
"""
import re
import struct
import sys

# Opcode families whose first operand is the destination register at bits 16-23.
DEST_FIRST = ('MOV', 'FMUL', 'FADD', 'FFMA', 'IMAD', 'IADD3', 'LOP3', 'SEL', 'FSEL', 'FMNMX', 'MUFU', 'IPA', 'LDC',
              'SHF', 'LEA', 'S2R', 'CS2R', 'F2I', 'I2FP', 'PRMT', 'POPC', 'FRND', 'F2FP', 'HADD2', 'IABS', 'IMNMX',
              'FSET', 'FLO', 'I2F')
LINE = re.compile(r'/\*([0-9a-f]{4,})\*/\s+(.*?)\s*;')


def number(text):
    text = text.strip()
    if text.startswith(('0x', '-0x')):
        return float(int(text, 16))
    if text in ('+INF', 'INF'):
        return float('inf')
    if text == '-INF':
        return float('-inf')
    if text in ('+QNAN', 'QNAN', '-QNAN'):
        return None
    return float(text)


def main(dis_path, fields_path):
    fields = [line.split() for line in open(fields_path, encoding='utf-8')]
    texts = {}
    for line in open(dis_path, encoding='utf-8', errors='replace'):
        m = LINE.search(line)
        if m:
            texts[int(m.group(1), 16) // 16] = m.group(2)
    counts = dict(instructions=len(fields), disassembled=0, predicate=0, predicate_bad=0, dest=0, dest_bad=0,
                  const=0, const_bad=0, imm=0, imm_bad=0)
    bad = []
    for index, f in enumerate(fields):
        text = texts.get(index)
        if text is None:
            bad.append((index, 'no text', f))
            continue
        counts['disassembled'] += 1
        opcode = int(f[1], 16)
        pred, negated = int(f[2][1]), f[2].endswith('!')
        words = text.split()
        guard = None
        if words[0].startswith('@'):
            guard = words.pop(0)
        want = None if pred == 7 and not negated else '@{}{}'.format('!' if negated else '', 'PT' if pred == 7 else 'P%d' % pred)
        counts['predicate'] += 1
        if guard and words[0].startswith('U'):
            guard = guard.replace('UP', 'P')   # uniform datapath: the same bits name a uniform predicate
        if guard != want:
            counts['predicate_bad'] += 1
            bad.append((index, 'predicate', text, f))
        mnemonic = words[0].split('.')[0]
        operands = ' '.join(words[1:]).split(',')
        if mnemonic in DEST_FIRST and operands and operands[0].strip():
            dest = int(f[3][1:])
            first = operands[0].strip().split()[0]
            want_reg = 'RZ' if dest == 255 else 'R%d' % dest
            if first.startswith('R') and not first.startswith('RZ.'):
                counts['dest'] += 1
                if first.rstrip('.reuse') != want_reg and first.split('.')[0] != want_reg:
                    counts['dest_bad'] += 1
                    bad.append((index, 'dest', text, f))
        group = opcode >> 8
        cm = re.search(r'c\[0x([0-9a-f]+)\]\[0x([0-9a-f]+)\]', text)
        if cm and group in (0xa,) and mnemonic in DEST_FIRST:
            counts['const'] += 1
            bank, offset = f[7][1:].split(':')
            if int(cm.group(1), 16) != int(bank) or int(cm.group(2), 16) != int(offset, 16):
                counts['const_bad'] += 1
                bad.append((index, 'const', text, f))
        if group == 0x8 and mnemonic in ('MOV', 'FMUL', 'FADD', 'FFMA'):
            imm = int(f[6][3:], 16)
            last = operands[1].strip() if mnemonic != 'FFMA' else (operands[2].strip() if len(operands) > 2 else '')
            if mnemonic == 'MOV':
                last = operands[-1].strip()
                got = int(last, 16) if last.startswith('0x') else None
                if got is not None:
                    counts['imm'] += 1
                    if got != imm:
                        counts['imm_bad'] += 1
                        bad.append((index, 'imm', text, f))
            else:
                try:
                    got = number(last.split()[0].replace('|', ''))
                except (ValueError, IndexError):
                    got = None
                if got is not None:
                    counts['imm'] += 1
                    value = struct.unpack('<f', struct.pack('<I', imm))[0]
                    if abs(value - got) > 1e-6 * max(1.0, abs(got)):
                        counts['imm_bad'] += 1
                        bad.append((index, 'imm', text, f))
    print(counts)
    for row in bad[:15]:
        print(row)
    return 1 if any(counts[k] for k in ('predicate_bad', 'dest_bad', 'const_bad', 'imm_bad')) else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1], sys.argv[2]))
