"""Minimal XDVDFS (Xbox / Xbox 360 game disc) reader.

usage: python xdvdfs.py list <image.iso>
       python xdvdfs.py extract <image.iso> <out_dir> [glob ...]
"""
import fnmatch
import os
import struct
import sys

SECTOR = 2048
MAGIC = b"MICROSOFT*XBOX*MEDIA"
# Game partition offsets: plain XISO, XGD3, XGD2, XGD1.
PARTITION_OFFSETS = (0, 0x2080000, 0xFD90000, 0x18300000)
ATTR_DIRECTORY = 0x10


class Entry:
    def __init__(self, path, offset, size, is_dir):
        self.path = path
        self.offset = offset  # absolute byte offset in the image
        self.size = size
        self.is_dir = is_dir


def find_partition(f):
    for base in PARTITION_OFFSETS:
        f.seek(base + 32 * SECTOR)
        if f.read(len(MAGIC)) == MAGIC:
            root_sector, root_size = struct.unpack("<II", f.read(8))
            return base, root_sector, root_size
    raise ValueError("not an Xbox / Xbox 360 disc image (no XDVDFS volume)")


def _walk(f, base, sector, size, prefix):
    f.seek(base + sector * SECTOR)
    data = f.read(size)
    stack = [0]
    seen = set()
    while stack:
        off = stack.pop()
        if off in seen or off + 14 > len(data):
            continue
        seen.add(off)
        left, right, start, length, attr, name_len = struct.unpack_from(
            "<HHIIBB", data, off)
        if left == 0xFFFF:  # sector padding
            continue
        name = data[off + 14:off + 14 + name_len].decode("latin-1")
        path = prefix + name
        if attr & ATTR_DIRECTORY:
            yield Entry(path + "/", base + start * SECTOR, length, True)
            if length:
                yield from _walk(f, base, start, length, path + "/")
        else:
            yield Entry(path, base + start * SECTOR, length, False)
        if left:
            stack.append(left * 4)
        if right:
            stack.append(right * 4)


def list_entries(f):
    """All entries of the image, sorted by path."""
    base, root_sector, root_size = find_partition(f)
    return sorted(_walk(f, base, root_sector, root_size, ""),
                  key=lambda e: e.path)


def read_file(f, entry):
    f.seek(entry.offset)
    return f.read(entry.size)


def extract(f, entry, dst, progress=None):
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    f.seek(entry.offset)
    remaining = entry.size
    with open(dst, "wb") as out:
        while remaining:
            chunk = f.read(min(remaining, 8 << 20))
            if not chunk:
                raise IOError(f"short read in {entry.path}")
            out.write(chunk)
            remaining -= len(chunk)
            if progress:
                progress(len(chunk))


def main():
    cmd, iso = sys.argv[1], sys.argv[2]
    with open(iso, "rb") as f:
        entries = list_entries(f)
        if cmd == "list":
            for e in entries:
                print(f"{'' if e.is_dir else e.size:>12}  {e.path}")
            return
        out_dir, patterns = sys.argv[3], sys.argv[4:] or ["*"]
        for e in entries:
            if e.is_dir or not any(fnmatch.fnmatch(e.path.lower(), p.lower())
                                   for p in patterns):
                continue
            extract(f, e, os.path.join(out_dir, *e.path.split("/")))
            print(f"{e.size:>12}  {e.path}")


if __name__ == "__main__":
    main()
