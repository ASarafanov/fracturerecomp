"""Reconcile scanned function starts with a codegen log.

- A scan hit can be a switch label or other spot inside a function rather
  than a function start. Codegen then reports branches that cross the new
  boundary ("Unresolved conditional branch to X from Y", "Unresolved b target
  X from Y"). For each one this removes the entries that lie between source
  and target, or, when none does, the nearest entry at or below both (the
  entry that starts the function the branch belongs to).
- A scan hit can also be a thunk that tail-calls a function nothing else
  reaches. Validation then fails with "X from Y: b X ... - target not in any
  function"; this adds X as a new entry.

usage: python tools/dev/prune_splits.py <codegen log> <config.toml>
"""
import bisect
import re
import sys

log_path, config_path = sys.argv[1], sys.argv[2]
log = open(log_path, encoding="utf-8", errors="replace").read()
text = open(config_path, encoding="utf-8").read()
entry_re = re.compile(r'^"0x([0-9A-F]+)" = \{\}$', re.M)
entries = sorted(int(a, 16) for a in entry_re.findall(text))

branches = {(int(s, 16), int(t, 16)) for t, s in re.findall(
    r"Unresolved (?:conditional branch to|b target) 0x([0-9A-F]+) "
    r"from 0x([0-9A-F]+)", log)}
missing = {int(t, 16) for t in re.findall(
    r"^\s+0x([0-9A-F]+) from 0x[0-9A-F]+: b .* - target not in any function",
    log, re.M)}

drop = set()
for src, dst in branches:
    lo, hi = min(src, dst), max(src, dst)
    between = [e for e in entries if lo < e <= hi]
    if between:
        drop.update(between)
        continue
    i = bisect.bisect_right(entries, lo) - 1
    if i >= 0:
        drop.add(entries[i])
add = missing - set(entries)

kept = sorted(set(entries) - drop | add)
lines = [l for l in text.splitlines() if not entry_re.match(l)]
while lines and not lines[-1]:
    lines.pop()
lines += [""] + [f'"0x{e:08X}" = {{}}' for e in kept]
open(config_path, "w", encoding="utf-8", newline="\n").write(
    "\n".join(lines) + "\n")
print(f"{len(branches)} cross-function branches: dropped {len(drop)}"
      + "".join(f" 0x{d:08X}" for d in sorted(drop)))
print(f"{len(missing)} missing tail-call targets: added {len(add)}"
      + "".join(f" 0x{a:08X}" for a in sorted(add)))
print(f"{len(kept)} entries")
