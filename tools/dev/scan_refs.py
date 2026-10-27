"""Find guest functions that codegen did not discover because nothing calls
them directly: their address is only stored in data (vtables, static
initializer tables, callback tables) or built in code with lis + addi/ori
(callbacks, pointers to virtual member functions).

usage: python tools/dev/scan_refs.py [--code] <image.bin> <generated/default dir>
                                     [--check ADDR ...]

image.bin is the guest image dumped from 0x82000000 (--fracture_dump_image).
Without --code it scans data words; with --code it scans lis/addi and lis/ori
pairs inside known functions. Prints TOML [functions] entries (start-only)
for each candidate.
"""
import argparse
import bisect
import collections
import glob
import json
import os
import re
import struct
import sys

IMAGE_BASE = 0x82000000
CODE_START, CODE_END = 0x821E0000, 0x832829F4
MAX_WALK = 4096  # instructions

parser = argparse.ArgumentParser()
parser.add_argument("--code", action="store_true",
                    help="scan lis/addi pairs in code instead of data words")
parser.add_argument("image")
parser.add_argument("gen_dir")
parser.add_argument("--check", nargs="*", default=[],
                    help="addresses to report on (hex)")
parser.add_argument("--show-rejected", action="store_true",
                    help="list rejected targets and the words up to the "
                         "next known function")
args = parser.parse_args()
checks = [int(a, 16) for a in args.check]
image = open(args.image, "rb").read()


def word(addr):
    off = addr - IMAGE_BASE
    return struct.unpack_from(">I", image, off)[0]


# Known functions: start from the partition, size from the emitted asm lines.
starts = sorted(int(k, 16) for k in json.load(
    open(os.path.join(args.gen_dir, "codegen.partition.json")))["assignments"])
sizes = {}
func_re = re.compile(r"DEFINE_REX_FUNC\(sub_([0-9A-F]+)\) \{\n(.*?)\n\}\n", re.S)
for path in glob.glob(os.path.join(args.gen_dir, "*_recomp.*.cpp")):
    text = open(path, encoding="utf-8", errors="replace").read()
    for m in func_re.finditer(text):
        n = sum(1 for l in m.group(2).splitlines() if l.strip().startswith("// "))
        sizes[int(m.group(1), 16)] = 4 * n
ends = []
for i, s in enumerate(starts):
    nxt = starts[i + 1] if i + 1 < len(starts) else CODE_END
    ends.append(min(s + sizes.get(s, 4), nxt))


def known_function_at(addr):
    i = bisect.bisect_right(starts, addr) - 1
    return i >= 0 and addr < ends[i]


def is_terminator(w):
    if w in (0x4E800020, 0x4E800420):  # blr, bctr
        return True
    return (w >> 26) == 18 and (w & 3) == 0  # b (not bl / ba)


def plausible_code(addr):
    """Walk forward to an unconditional terminator without hitting padding."""
    nxt = starts[bisect.bisect_right(starts, addr)] if addr < starts[-1] else CODE_END
    for i in range(MAX_WALK):
        a = addr + 4 * i
        if a >= nxt:
            return False
        w = word(a)
        if w == 0 or (w >> 26) == 0:  # padding / illegal primary opcode 0
            return False
        if is_terminator(w):
            return True
    return False


def data_refs():
    """Every aligned word that points into code, excluding words that are
    themselves instructions of a known function."""
    refs = collections.defaultdict(list)
    for off in range(0, len(image) - 3, 4):
        src = IMAGE_BASE + off
        if CODE_START <= src < CODE_END and known_function_at(src):
            continue
        v = struct.unpack_from(">I", image, off)[0]
        if CODE_START <= v < CODE_END and v % 4 == 0:
            refs[v].append(src)
    return refs


# Primary opcodes whose D/S field (bits 21-25) is a destination register:
# mulli, subfic, addic, addic., addi, addis, lwz(u), lbz(u), lhz(u), lha(u),
# lmw, ld/ldu/lwa.
WRITES_RD = {7, 8, 12, 13, 14, 15, 32, 33, 34, 35, 40, 41, 42, 43, 46, 58}
# Primary opcodes whose A field (bits 16-20) is a destination register:
# load-with-update forms, rotates, and logical immediates.
WRITES_RA = {33, 35, 41, 43, 20, 21, 23, 24, 25, 26, 27, 28, 29, 30}
VOLATILE_GPRS = (0, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12)


def code_refs():
    """Addresses built by "lis rX,hi" and a later "addi rY,rX,lo" or
    "ori rY,rX,lo" in the same known function. The compiler often keeps the
    high half in a non-volatile register far from its use, so track lis
    values through the whole function, dropping a register when something
    else writes it (approximately) and dropping volatile ones at calls."""
    refs = collections.defaultdict(list)
    for start, end in zip(starts, ends):
        regs = {}
        for a in range(start, end, 4):
            w = word(a)
            op, rd, ra = w >> 26, (w >> 21) & 31, (w >> 16) & 31
            imm = w & 0xFFFF
            if op == 14 and ra and ra in regs:  # addi rD,rX,SIMM
                refs[(regs[ra] + imm - (0x10000 if imm & 0x8000 else 0))
                     & 0xFFFFFFFF].append(a)
            elif op == 24 and rd in regs:  # ori rA,rX,UIMM
                refs[regs[rd] | imm].append(a)
            if op == 18 and w & 1:  # bl: callee may clobber volatile GPRs
                for r in VOLATILE_GPRS:
                    regs.pop(r, None)
            written = set()
            if op in WRITES_RD:
                written.add(rd)
            if op in WRITES_RA:
                written.add(ra)
            if op == 31:  # X-form: destination is D or A depending on the op
                written |= {rd, ra}
            for r in written:
                regs.pop(r, None)
            if op == 15 and ra == 0:  # lis rD,SIMM
                regs[rd] = imm << 16
    return {t: s for t, s in refs.items()
            if CODE_START <= t < CODE_END and t % 4 == 0}


refs = code_refs() if args.code else data_refs()
candidates = sorted(t for t in refs
                    if not known_function_at(t) and plausible_code(t))
candidate_set = set(candidates)
rejected = sorted(t for t in refs
                  if not known_function_at(t) and t not in candidate_set)

kind = "lis/addi pairs" if args.code else "data words"
print(f"# known functions: {len(starts)}", file=sys.stderr)
print(f"# {kind} pointing into code: {sum(map(len, refs.values()))} "
      f"to {len(refs)} distinct targets", file=sys.stderr)
print(f"# candidates (in a gap, plausible code): {len(candidates)}",
      file=sys.stderr)
print(f"# rejected (in a gap, not plausible code): {len(rejected)}",
      file=sys.stderr)
if args.show_rejected:
    for t in rejected:
        nxt = starts[bisect.bisect_right(starts, t)]
        words = " ".join(f"{word(a):08X}" for a in range(t, min(nxt, t + 64), 4))
        print(f"# rejected 0x{t:08X} (next known 0x{nxt:08X}): {words}",
              file=sys.stderr)
for c in checks:
    print(f"# check 0x{c:08X}: referenced={c in refs} "
          f"known={known_function_at(c)} candidate={c in candidate_set} "
          f"from={[hex(s) for s in refs.get(c, [])[:4]]}", file=sys.stderr)
for t in candidates:
    print(f'"0x{t:08X}" = {{}}')
