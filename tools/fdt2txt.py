#!/usr/bin/env python3
"""Dump a flattened device tree (DTB) as indented text, without dtc.

Strings print as text, everything else as 32-bit big-endian cells.
Usage:  fdt2txt.py IN.dtb > OUT.txt
"""
import struct, sys


def dump(blob, out):
    magic, _, off_struct, off_strings = struct.unpack_from('>4I', blob, 0)
    if magic != 0xd00dfeed:
        raise SystemExit('not a DTB')
    pos, depth = off_struct, 0
    while True:
        tok, = struct.unpack_from('>I', blob, pos)
        pos += 4
        if tok == 1:  # FDT_BEGIN_NODE
            end = blob.index(b'\0', pos)
            name = blob[pos:end].decode(errors='replace') or '/'
            out.write('  ' * depth + name + ' {\n')
            pos = (end + 4) & ~3
            depth += 1
        elif tok == 2:  # FDT_END_NODE
            depth -= 1
            out.write('  ' * depth + '}\n')
        elif tok == 3:  # FDT_PROP
            ln, nameoff = struct.unpack_from('>2I', blob, pos)
            pos += 8
            val = blob[pos:pos + ln]
            pos = (pos + ln + 3) & ~3
            name = blob[off_strings + nameoff:blob.index(b'\0', off_strings + nameoff)].decode()
            out.write('  ' * depth + name + fmt(val) + ';\n')
        elif tok == 4:  # FDT_NOP
            continue
        elif tok == 9:  # FDT_END
            return
        else:
            raise SystemExit(f'bad token {tok:#x} at {pos - 4:#x}')


def fmt(val):
    if not val:
        return ''
    parts = val[:-1].split(b'\0') if val.endswith(b'\0') else None
    if parts and all(p and all(32 <= c < 127 for c in p) for p in parts):
        return ' = ' + ', '.join('"%s"' % p.decode() for p in parts)
    if len(val) % 4 == 0:
        return ' = <' + ' '.join('%#x' % c for c in struct.unpack('>%dI' % (len(val) // 4), val)) + '>'
    return ' = [' + val.hex(' ') + ']'


if __name__ == '__main__':
    dump(open(sys.argv[1], 'rb').read(), sys.stdout)
