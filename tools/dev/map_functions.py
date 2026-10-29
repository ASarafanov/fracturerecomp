"""Match the functions of two releases' codegen output by their code.

Both releases come from nearly the same source, so most functions have the
same instructions; only absolute addresses (branch targets, lis/addi pairs,
offsets from them) move. Each function becomes a sequence of instruction
shapes (mnemonic plus registers, immediates and addresses blanked out), and
the two releases' function lists, in address order, are aligned like a diff.

usage: python tools/dev/map_functions.py <generated dir A> <generated dir B>
                                         [ADDR_A ...]

Without addresses it prints how many functions matched. With addresses it
prints, for each address of release A (a function start or any address
inside one), the matching address in release B. An address in no matched
function (code that only one side's codegen made a function of) is moved
by the shift of the matched functions around it when both agree, marked
"(by neighbours)"; otherwise it prints "?".
"""
import bisect
import difflib
import glob
import os
import re
import sys

func_re = re.compile(r"DEFINE_REX_FUNC\(sub_([0-9A-F]+)\) \{\n(.*?)\n\}\n", re.S)
reg_re = re.compile(r"\b(r\d+|f\d+|v\d+|cr\d|ctr|lr|xer)\b")


def shape(asm):
    """Mnemonic and register operands of one '// insn ops' line."""
    mnemonic, _, ops = asm.partition(" ")
    return mnemonic + " " + ",".join(reg_re.findall(ops))


def load(gen_dir):
    """{start: [(offset in function, shape), ...]} for every function."""
    funcs = {}
    for path in glob.glob(os.path.join(gen_dir, "*_recomp.*.cpp")):
        text = open(path, encoding="utf-8", errors="replace").read()
        for m in func_re.finditer(text):
            asm = [l.strip()[3:] for l in m.group(2).splitlines()
                   if l.strip().startswith("// ")]
            funcs[int(m.group(1), 16)] = [shape(a) for a in asm]
    return funcs


def align(funcs_a, funcs_b):
    starts_a, starts_b = sorted(funcs_a), sorted(funcs_b)
    keys_a = [hash(tuple(funcs_a[s])) for s in starts_a]
    keys_b = [hash(tuple(funcs_b[s])) for s in starts_b]
    matcher = difflib.SequenceMatcher(None, keys_a, keys_b, autojunk=False)
    mapping = {}
    for block in matcher.get_matching_blocks():
        for k in range(block.size):
            mapping[starts_a[block.a + k]] = starts_b[block.b + k]
    return starts_a, mapping


def main():
    dir_a, dir_b = sys.argv[1], sys.argv[2]
    funcs_a, funcs_b = load(dir_a), load(dir_b)
    starts_a, mapping = align(funcs_a, funcs_b)
    queries = [int(a, 16) for a in sys.argv[3:]]
    if not queries:
        print(f"{len(mapping)} of {len(funcs_a)} functions of A matched "
              f"({len(funcs_b)} in B)")
        return
    matched = sorted(mapping)
    for addr in queries:
        i = bisect.bisect_right(starts_a, addr) - 1
        start = starts_a[i] if i >= 0 else None
        if start is not None and start in mapping \
                and addr < start + 4 * len(funcs_a[start]):
            target = mapping[start] + (addr - start)
            note = "" if addr == start else f" (sub_{start:08X}+0x{addr - start:X})"
            print(f"0x{addr:08X} -> 0x{target:08X}{note}")
            continue
        j = bisect.bisect_right(matched, addr)
        if 0 < j < len(matched):
            before, after = matched[j - 1], matched[j]
            shift = mapping[before] - before
            if mapping[after] - after == shift:
                print(f"0x{addr:08X} -> 0x{addr + shift:08X} (by neighbours)")
                continue
        print(f"0x{addr:08X} -> ?")


if __name__ == "__main__":
    main()
