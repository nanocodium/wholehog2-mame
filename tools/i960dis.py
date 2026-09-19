"""Minimal Intel i960 disassembler (enough to read boot code): REG, MEM (A/B), CTRL and COBR formats with mnemonic
tables for the common opcodes. usage: i960dis.py file base_addr file_offset [count]"""
import struct, sys

CTRL = {0x08: 'b', 0x09: 'call', 0x0a: 'ret', 0x0b: 'bal', 0x10: 'bno', 0x11: 'bg', 0x12: 'be', 0x13: 'bge', 0x14: 'bl', 0x15: 'bne', 0x16: 'ble', 0x17: 'bo',
        0x18: 'faultno', 0x19: 'faultg', 0x1a: 'faulte', 0x1b: 'faultge', 0x1c: 'faultl', 0x1d: 'faultne', 0x1e: 'faultle', 0x1f: 'faulto'}
COBR = {0x20: 'testno', 0x21: 'testg', 0x22: 'teste', 0x23: 'testge', 0x24: 'testl', 0x25: 'testne', 0x26: 'testle', 0x27: 'testo',
        0x30: 'bbc', 0x31: 'cmpobg', 0x32: 'cmpobe', 0x33: 'cmpobge', 0x34: 'cmpobl', 0x35: 'cmpobne', 0x36: 'cmpoble', 0x37: 'bbs',
        0x38: 'cmpibno', 0x39: 'cmpibg', 0x3a: 'cmpibe', 0x3b: 'cmpibge', 0x3c: 'cmpibl', 0x3d: 'cmpibne', 0x3e: 'cmpible', 0x3f: 'cmpibo'}
MEM = {0x80: 'ldob', 0x82: 'stob', 0x84: 'bx', 0x85: 'balx', 0x86: 'callx', 0x88: 'ldos', 0x8a: 'stos', 0x8c: 'lda', 0x90: 'ld', 0x92: 'st',
       0x98: 'ldl', 0x9a: 'stl', 0xa0: 'ldt', 0xa2: 'stt', 0xb0: 'ldq', 0xb2: 'stq', 0xc0: 'ldib', 0xc2: 'stib', 0xc8: 'ldis', 0xca: 'stis'}
REG = {0x580: 'notbit', 0x581: 'and', 0x582: 'andnot', 0x583: 'setbit', 0x584: 'notand', 0x586: 'xor', 0x587: 'or', 0x588: 'nor', 0x589: 'xnor', 0x58a: 'not',
       0x58b: 'ornot', 0x58c: 'clrbit', 0x58d: 'notor', 0x58e: 'nand', 0x58f: 'alterbit', 0x590: 'addo', 0x591: 'addi', 0x592: 'subo', 0x593: 'subi',
       0x598: 'shro', 0x59a: 'shrdi', 0x59b: 'shri', 0x59c: 'shlo', 0x59d: 'rotate', 0x59e: 'shli', 0x5a0: 'cmpo', 0x5a1: 'cmpi', 0x5a2: 'concmpo', 0x5a3: 'concmpi',
       0x5a4: 'cmpinco', 0x5a5: 'cmpinci', 0x5a6: 'cmpdeco', 0x5a7: 'cmpdeci', 0x5ac: 'scanbyte', 0x5ae: 'chkbit', 0x5b0: 'addc', 0x5b2: 'subc',
       0x5cc: 'mov', 0x5dc: 'movl', 0x5ec: 'movt', 0x5fc: 'movq', 0x600: 'synmov', 0x610: 'atmod', 0x612: 'atadd', 0x640: 'spanbit', 0x641: 'scanbit',
       0x642: 'daddc', 0x643: 'dsubc', 0x644: 'dmovt', 0x645: 'modac', 0x650: 'modify', 0x651: 'extract', 0x654: 'modtc', 0x655: 'modpc', 0x658: 'intctl',
       0x659: 'sysctl', 0x65b: 'icctl', 0x65c: 'dcctl', 0x660: 'calls', 0x66b: 'mark', 0x66c: 'fmark', 0x66d: 'flushreg', 0x66f: 'syncf', 0x670: 'emul',
       0x671: 'ediv', 0x701: 'mulo', 0x708: 'remo', 0x70b: 'divo', 0x741: 'muli', 0x748: 'remi', 0x749: 'modi', 0x74b: 'divi', 0x780: 'addono', 0x781: 'addino',
       0x782: 'subono', 0x783: 'subino', 0x784: 'selno', 0x790: 'addog', 0x794: 'selg', 0x7a0: 'addoe', 0x7a4: 'sele', 0x7b0: 'addoge', 0x7b4: 'selge',
       0x7c0: 'addol', 0x7c4: 'sell', 0x7d0: 'addone', 0x7d4: 'selne', 0x7e0: 'addole', 0x7e4: 'selle', 0x7f0: 'addoo', 0x7f4: 'selo'}
REGN = ['pfp', 'sp', 'rip', 'r3', 'r4', 'r5', 'r6', 'r7', 'r8', 'r9', 'r10', 'r11', 'r12', 'r13', 'r14', 'r15',
        'g0', 'g1', 'g2', 'g3', 'g4', 'g5', 'g6', 'g7', 'g8', 'g9', 'g10', 'g11', 'g12', 'g13', 'g14', 'fp']


def s32(v): return v - (1 << 32) if v & 0x80000000 else v


def decode(d, off, pc):
    w = struct.unpack_from('<I', d, off)[0]; op = w >> 24; n = 4
    if 0x08 <= op <= 0x1f:
        disp = (w & 0x00fffffc); disp = disp - 0x1000000 if disp & 0x800000 else disp
        return n, '%-8s %08x' % (CTRL.get(op, 'ctrl%02x' % op), (pc + disp) & 0xffffffff)
    if 0x20 <= op <= 0x3f:
        s1 = (w >> 19) & 31; s2 = (w >> 14) & 31; m1 = (w >> 13) & 1; disp = w & 0x1ffc; disp = disp - 0x2000 if disp & 0x1000 else disp
        a = str(s1) if m1 else REGN[s1]
        return n, '%-8s %s, %s, %08x' % (COBR.get(op, 'cobr%02x' % op), a, REGN[s2], (pc + disp) & 0xffffffff)
    if 0x58 <= op <= 0x7f:
        src1 = w & 31; s1 = (w >> 5) & 1; s2 = (w >> 6) & 1; oplo = (w >> 7) & 15; m1 = (w >> 11) & 1; m2 = (w >> 12) & 1; m3 = (w >> 13) & 1
        src2 = (w >> 14) & 31; dst = (w >> 19) & 31; full = (op << 4) | oplo
        a = str(src1) if m1 else REGN[src1]; b = str(src2) if m2 else REGN[src2]; c = REGN[dst]
        name = REG.get(full, 'reg%03x' % full)
        if name in ('mov', 'movl', 'movt', 'movq', 'not', 'scanbit', 'spanbit'): return n, '%-8s %s, %s' % (name, a, c)
        return n, '%-8s %s, %s, %s' % (name, a, b, c)
    if 0x80 <= op <= 0xcf:
        srcdst = (w >> 19) & 31; abase = (w >> 14) & 31; name = MEM.get(op, 'mem%02x' % op)
        if not (w & 0x1000):   # MEMA
            offset = w & 0xfff; md = (w >> 13) & 1
            ea = '%s(%s)' % (hex(offset), REGN[abase]) if md else hex(offset)
        else:                  # MEMB
            mode = (w >> 10) & 15; scale = 1 << ((w >> 7) & 7); idx = w & 31
            if mode in (0xc, 0xd, 0xe, 0xf, 0x5):
                disp = struct.unpack_from('<I', d, off + 4)[0]; n = 8
            if mode == 0x4: ea = '(%s)' % REGN[abase]
            elif mode == 0x5: ea = '%08x(ip)' % ((pc + 8 + s32(disp)) & 0xffffffff)
            elif mode == 0x7: ea = '(%s)[%s*%d]' % (REGN[abase], REGN[idx], scale)
            elif mode == 0xc: ea = '%08x' % disp
            elif mode == 0xd: ea = '%08x(%s)' % (disp, REGN[abase])
            elif mode == 0xe: ea = '%08x[%s*%d]' % (disp, REGN[idx], scale)
            elif mode == 0xf: ea = '%08x(%s)[%s*%d]' % (disp, REGN[abase], REGN[idx], scale)
            else: ea = 'mode%x' % mode
        if name in ('bx', 'callx'): return n, '%-8s %s' % (name, ea)
        if name.startswith('st'): return n, '%-8s %s, %s' % (name, REGN[srcdst], ea)
        return n, '%-8s %s, %s' % (name, ea, REGN[srcdst])
    return 4, '.word    %08x' % w


def main():
    f = sys.argv[1]; base = int(sys.argv[2], 16); off = int(sys.argv[3], 16); cnt = int(sys.argv[4]) if len(sys.argv) > 4 else 40
    d = open(f, 'rb').read(); pc = base
    for _ in range(cnt):
        n, txt = decode(d, off, pc)
        print('%08x  %s  %s' % (pc, d[off:off + n].hex(' '), txt)); off += n; pc += n


if __name__ == '__main__':
    main()
