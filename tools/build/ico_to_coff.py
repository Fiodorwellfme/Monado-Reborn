#!/usr/bin/env python3
"""Compile a Windows .ico file into an x86_64 COFF .o resource object (.rsrc).

Produces a standard PE/COFF object with RT_ICON (3) entries 1..N and RT_GROUP_ICON (14)
entry 1 (English US, 0x0409) using only the Python standard library, so Windows builds
with zig cc/c++ do not depend on windres or rc.exe being installed.

Usage:
    python tools/build/ico_to_coff.py <input.ico> <output.o> [--id 1]
"""
import argparse
import os
import struct
import sys

IMAGE_FILE_MACHINE_AMD64 = 0x8664
IMAGE_SCN_CNT_INITIALIZED_DATA = 0x00000040
IMAGE_SCN_MEM_READ = 0x40000000
IMAGE_REL_AMD64_ADDR32NB = 0x0003
IMAGE_SYM_CLASS_STATIC = 3

RT_ICON = 3
RT_GROUP_ICON = 14
LANG_EN_US = 0x0409
SUBDIR_FLAG = 0x80000000


def parse_ico(data: bytes):
    """Parse a Windows .ico payload into a list of (dir_entry_tuple, image_bytes)."""
    if len(data) < 6:
        raise ValueError('ICO file too short')
    reserved, img_type, count = struct.unpack_from('<HHH', data, 0)
    if reserved != 0 or img_type != 1 or count == 0:
        raise ValueError(f'Not a valid ICO file (reserved={reserved}, type={img_type}, count={count})')
    if len(data) < 6 + count * 16:
        raise ValueError('ICO directory truncated')
    entries = []
    for i in range(count):
        w, h, colors, res, planes, bpp, size, off = struct.unpack_from('<BBBBHHII', data, 6 + i * 16)
        if size == 0 or off + size > len(data):
            raise ValueError(f'ICO entry {i} out of bounds (off={off}, size={size}, total={len(data)})')
        entries.append(((w, h, colors, res, planes or 1, bpp or 32, size), data[off : off + size]))
    return entries


def build_rsrc_section(ico_data: bytes, group_id: int = 1):
    """Return (rsrc_bytes, reloc_offsets) for an .rsrc section embedding the ICO."""
    entries = parse_ico(ico_data)
    n = len(entries)

    # GRPICONDIR (6 bytes) + N * GRPICONDIRENTRY (14 bytes each)
    grp = bytearray(struct.pack('<HHH', 0, 1, n))
    for idx, ((w, h, colors, _res, planes, bpp, size), _blob) in enumerate(entries, start=1):
        grp.extend(struct.pack('<BBBBHHIH', w, h, colors, 0, planes, bpp, size, idx))
    grp_bytes = bytes(grp)

    # Resource tree:
    #   root dir: 2 entries (RT_ICON=3, RT_GROUP_ICON=14)
    #   type dir RT_ICON: N entries (IDs 1..N)
    #   type dir RT_GROUP_ICON: 1 entry (ID = group_id)
    #   lang dirs: (N + 1) dirs, 1 entry each (LANG_EN_US)
    #   data entries: (N + 1) IMAGE_RESOURCE_DATA_ENTRY structs (16 bytes each)
    #   raw blobs: N icon blobs + 1 group blob (4-byte aligned)
    root_dir_off = 0
    root_dir_size = 16 + 2 * 8

    icon_type_off = root_dir_off + root_dir_size
    icon_type_size = 16 + n * 8

    grp_type_off = icon_type_off + icon_type_size
    grp_type_size = 16 + 1 * 8

    lang_dirs_off = grp_type_off + grp_type_size
    lang_dir_size = 16 + 1 * 8
    num_leaves = n + 1

    data_entries_off = lang_dirs_off + num_leaves * lang_dir_size
    blobs_off = data_entries_off + num_leaves * 16

    def dir_hdr(num_id):
        return struct.pack('<IIHHHH', 0, 0, 0, 0, 0, num_id)

    rsrc = bytearray()
    # 1. Root directory
    rsrc.extend(dir_hdr(2))
    rsrc.extend(struct.pack('<II', RT_ICON, icon_type_off | SUBDIR_FLAG))
    rsrc.extend(struct.pack('<II', RT_GROUP_ICON, grp_type_off | SUBDIR_FLAG))

    # 2. RT_ICON type directory (IDs 1..N)
    rsrc.extend(dir_hdr(n))
    for i in range(n):
        rsrc.extend(struct.pack('<II', i + 1, (lang_dirs_off + i * lang_dir_size) | SUBDIR_FLAG))

    # 3. RT_GROUP_ICON type directory (ID = group_id)
    rsrc.extend(dir_hdr(1))
    rsrc.extend(struct.pack('<II', group_id, (lang_dirs_off + n * lang_dir_size) | SUBDIR_FLAG))

    # 4. Language directories (N for RT_ICON + 1 for RT_GROUP_ICON)
    for i in range(num_leaves):
        rsrc.extend(dir_hdr(1))
        rsrc.extend(struct.pack('<II', LANG_EN_US, data_entries_off + i * 16))

    assert len(rsrc) == data_entries_off

    # 5. Blobs layout & IMAGE_RESOURCE_DATA_ENTRY leaves
    blobs = [blob for _, blob in entries] + [grp_bytes]
    reloc_offsets = []
    blob_payload = bytearray()
    cur_blob_off = blobs_off
    for i, blob in enumerate(blobs):
        reloc_offsets.append(data_entries_off + i * 16)
        rsrc.extend(struct.pack('<IIII', cur_blob_off, len(blob), 0, 0))
        blob_payload.extend(blob)
        pad = (-len(blob)) & 3
        if pad:
            blob_payload.extend(b'\x00' * pad)
        cur_blob_off += len(blob) + pad

    assert len(rsrc) == blobs_off
    rsrc.extend(blob_payload)
    return bytes(rsrc), reloc_offsets


def build_coff_object(ico_data: bytes, group_id: int = 1) -> bytes:
    """Build a complete x86_64 COFF .o object containing the .rsrc section."""
    rsrc, relocs = build_rsrc_section(ico_data, group_id=group_id)
    raw_off = 20 + 40  # IMAGE_FILE_HEADER + 1 * IMAGE_SECTION_HEADER
    reloc_off = raw_off + len(rsrc)
    symtab_off = reloc_off + len(relocs) * 10

    file_hdr = struct.pack('<HHIIIHH', IMAGE_FILE_MACHINE_AMD64, 1, 0, symtab_off, 1, 0, 0)
    sec_hdr = struct.pack(
        '<8sIIIIIIHHI',
        b'.rsrc\x00\x00\x00',
        0,
        0,
        len(rsrc),
        raw_off,
        reloc_off,
        0,
        len(relocs),
        0,
        IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ,
    )
    reloc_bytes = b''.join(struct.pack('<IIH', off, 0, IMAGE_REL_AMD64_ADDR32NB) for off in relocs)
    sym_bytes = struct.pack('<8sIhHBB', b'.rsrc\x00\x00\x00', 0, 1, 0, IMAGE_SYM_CLASS_STATIC, 0)
    strtab = struct.pack('<I', 4)

    return file_hdr + sec_hdr + rsrc + reloc_bytes + sym_bytes + strtab


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description='Compile a Windows .ico into an x86_64 COFF .rsrc object.')
    ap.add_argument('ico', help='Input .ico path')
    ap.add_argument('out', help='Output COFF .o path')
    ap.add_argument('--id', type=int, default=1, help='RT_GROUP_ICON resource ID (default: 1)')
    args = ap.parse_args(argv)

    with open(args.ico, 'rb') as f:
        ico_data = f.read()
    coff = build_coff_object(ico_data, group_id=args.id)
    out_dir = os.path.dirname(os.path.abspath(args.out))
    if out_dir:
        os.makedirs(out_dir, exist_ok=True)
    with open(args.out, 'wb') as f:
        f.write(coff)
    return 0


if __name__ == '__main__':
    sys.exit(main())
