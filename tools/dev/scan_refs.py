"""Find guest functions that are referenced only from data (vtables, static
initializer tables, callback tables) and that codegen did not discover.

usage: python tools/dev/scan_refs.py <image.bin> <generated/default dir> [check addr ...]

image.bin is the guest image dumped from 0x82000000 (--fracture_dump_image).
Prints TOML [functions] entries (start-only) for each candidate.
"""
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

image_path, gen_dir = sys.argv[1], sys.argv[2]
checks = [int(a, 16) for a in sys.argv[3:]]
image = open(image_path, "rb").read()


def word(addr):
    off = addr - IMAGE_BASE
    return struct.unpack_from(">I", image, off)[0]


# Known functions: start from the partition, size from the emitted asm lines.
starts = sorted(int(k, 16) for k in json.load(
    open(os.path.join(gen_dir, "codegen.partition.json")))["assignments"])
sizes = {}
func_re = re.compile(r"DEFINE_REX_FUNC\(sub_([0-9A-F]+)\) \{\n(.*?)\n\}\n", re.S)
for path in glob.glob(os.path.join(gen_dir, "*_recomp.*.cpp")):
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


# Every aligned word that points into code, excluding words that are
# themselves instructions of a known function.
refs = collections.defaultdict(list)
for off in range(0, len(image) - 3, 4):
    src = IMAGE_BASE + off
    if CODE_START <= src < CODE_END and known_function_at(src):
        continue
    v = struct.unpack_from(">I", image, off)[0]
    if CODE_START <= v < CODE_END and v % 4 == 0:
        refs[v].append(src)

candidates = sorted(t for t in refs
                    if not known_function_at(t) and plausible_code(t))
rejected = sorted(t for t in refs
                  if not known_function_at(t) and t not in set(candidates))

from_code_gaps = sum(1 for t in candidates
                     if all(CODE_START <= s < CODE_END for s in refs[t]))
print(f"# known functions: {len(starts)}", file=sys.stderr)
print(f"# data words pointing into code: {sum(map(len, refs.values()))} "
      f"to {len(refs)} distinct targets", file=sys.stderr)
print(f"# candidates (in a gap, plausible code): {len(candidates)} "
      f"({from_code_gaps} referenced only from inside the code range)",
      file=sys.stderr)
print(f"# rejected (in a gap, not plausible code): {len(rejected)}",
      file=sys.stderr)
for c in checks:
    print(f"# check 0x{c:08X}: referenced={c in refs} "
          f"known={known_function_at(c)} candidate={c in candidates} "
          f"from={[hex(s) for s in refs.get(c, [])[:4]]}", file=sys.stderr)
for t in candidates:
    print(f'"0x{t:08X}" = {{}}')
