"""Find generated functions that keep going past a call to a no-return
function (e.g. the C++ rethrow helper) into the EH data words that follow it.

usage: python tools/dev/fallthrough.py <generated/default dir> <noreturn addr hex>
"""
import glob
import os
import re
import sys

gen_dir, target = sys.argv[1], sys.argv[2].lower().removeprefix("0x")
call = f"// bl 0x{target}"
func_re = re.compile(r"DEFINE_REX_FUNC\(sub_([0-9A-F]+)\) \{\n(.*?)\n\}\n", re.S)

hits = []
for path in glob.glob(os.path.join(gen_dir, "fracture_recomp.*.cpp")):
    text = open(path, encoding="utf-8", errors="replace").read()
    for m in func_re.finditer(text):
        lines = [l.strip() for l in m.group(2).splitlines()]
        for i, line in enumerate(lines):
            if line != call or i + 1 >= len(lines):
                continue
            lr = re.match(r"ctx\.lr = 0x([0-9A-F]+);", lines[i + 1])
            rest = [l for l in lines[i + 2:] if l.startswith("// ")]
            # Anything emitted after the call means the analyzer assumed the
            # call returns; the first word after it is the EH handler pointer.
            if lr and rest and rest[0].startswith("// lwz r17,"):
                hits.append((int(m.group(1), 16), int(lr.group(1), 16)))
                break

for start, end in sorted(set(hits)):
    print(f'"0x{start:08X}" = {{ end = 0x{end:08X} }}')
print(f"# {len(set(hits))} functions", file=sys.stderr)
