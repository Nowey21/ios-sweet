#!/usr/bin/env python3
"""Extract one logical partition from Android dynamic-partition (liblp) images.

Works on a retrofit super split across several physical partitions (davinci:
the metadata lives on `system`, extents also land on `vendor`).

  lpextract.py OUT.img NAME METADATA_DEV.img [OTHER_DEV.img ...]

Each image is matched to a block device of the metadata by its file name
(system.img -> "system"). Read-only on the inputs.
"""
import os, struct, sys

LP_GEOMETRY_MAGIC = 0x616c4467
LP_HEADER_MAGIC = 0x414c5030
SECTOR = 512


def parse(meta):
    geo = meta[4096:4096 + 52]
    magic, _, = struct.unpack_from('<II', geo, 0)
    if magic != LP_GEOMETRY_MAGIC:
        raise SystemExit('no LP geometry at 4096')
    # Primary metadata, slot 0, follows the primary and backup geometry.
    h = 4096 * 3
    magic, major, minor, hsize = struct.unpack_from('<IHHI', meta, h)
    if magic != LP_HEADER_MAGIC:
        raise SystemExit('no LP header at %#x' % h)
    desc = struct.unpack_from('<12I', meta, h + 4 + 2 + 2 + 4 + 32 + 4 + 32)
    tables = h + hsize
    tab = lambda i: (tables + desc[i * 3], desc[i * 3 + 1], desc[i * 3 + 2])
    parts, exts, devs = [], [], []
    off, n, sz = tab(0)
    for i in range(n):
        name, attr, first, num, grp = struct.unpack_from('<36sIIII', meta, off + i * sz)
        parts.append((name.split(b'\0')[0].decode(), first, num))
    off, n, sz = tab(1)
    for i in range(n):
        exts.append(struct.unpack_from('<QIQI', meta, off + i * sz))
    off, n, sz = tab(3)
    for i in range(n):
        first_sec, align, align_off, size, name, flags = struct.unpack_from('<QIIQ36sI', meta, off + i * sz)
        devs.append(name.split(b'\0')[0].decode())
    return (major, minor), parts, exts, devs


def main():
    out, want, files = sys.argv[1], sys.argv[2], sys.argv[3:]
    with open(files[0], 'rb') as f:
        meta = f.read(4096 * 3 + 65536 * 2)
    ver, parts, exts, devs = parse(meta)
    print('LP metadata v%d.%d, block devices %s' % (ver + (devs,)))
    for name, first, num in parts:
        size = sum(exts[first + i][0] for i in range(num)) * SECTOR
        print('  %-20s %10d MiB, %d extent(s)' % (name, size >> 20, num))
    byname = {os.path.basename(p).rsplit('.', 1)[0]: p for p in files}
    match = [p for p in parts if p[0] == want]
    if not match:
        raise SystemExit('no partition %s' % want)
    _, first, num = match[0]
    with open(out, 'wb') as o:
        for i in range(num):
            nsec, ttype, tdata, tsrc = exts[first + i]
            if ttype != 0:  # LP_TARGET_TYPE_ZERO
                o.write(b'\0' * nsec * SECTOR)
                continue
            src = byname[devs[tsrc]]
            with open(src, 'rb') as f:
                f.seek(tdata * SECTOR)
                left = nsec * SECTOR
                while left:
                    chunk = f.read(min(left, 1 << 22))
                    if not chunk:
                        raise SystemExit('short read in %s' % src)
                    o.write(chunk)
                    left -= len(chunk)
            print('  extent %d: %d sectors from %s @ sector %d' % (i, nsec, devs[tsrc], tdata))
    print('%s -> %s (%d bytes)' % (want, out, os.path.getsize(out)))


if __name__ == '__main__':
    main()
