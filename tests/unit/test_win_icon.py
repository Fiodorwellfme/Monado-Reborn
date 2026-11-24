"""Unit tests for the Windows Monado icon (.ico -> COFF .rsrc object) and embedded header."""
import os
import re
import struct
import sys
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, 'tools', 'build'))

import ico_to_coff  # noqa: E402


class TestWindowsIcon(unittest.TestCase):
    ICO_PATH = os.path.join(ROOT, 'packaging', 'windows', 'monado.ico')
    RC_PATH = os.path.join(ROOT, 'packaging', 'windows', 'monado.rc')
    HEADER_PATH = os.path.join(ROOT, 'runtime', 'ui', 'monado_icon.h')

    def test_ico_contains_expected_sizes(self):
        with open(self.ICO_PATH, 'rb') as f:
            data = f.read()
        entries = ico_to_coff.parse_ico(data)
        sizes = [256 if meta[0] == 0 else meta[0] for meta, _ in entries]
        self.assertEqual(sizes, [16, 24, 32, 48, 64, 128, 256])

    def test_coff_object_structure(self):
        with open(self.ICO_PATH, 'rb') as f:
            ico_data = f.read()
        coff = ico_to_coff.build_coff_object(ico_data, group_id=1)

        machine, nsec, _ts, sym_off, nsym, opt_sz, _chars = struct.unpack_from('<HHIIIHH', coff, 0)
        self.assertEqual(machine, ico_to_coff.IMAGE_FILE_MACHINE_AMD64)
        self.assertEqual(nsec, 1)
        self.assertEqual(nsym, 1)
        self.assertEqual(opt_sz, 0)
        self.assertGreater(sym_off, 60)

        sec_name, _vs, _va, raw_sz, raw_off, reloc_off, _lno, nreloc, _nlno, sec_chars = struct.unpack_from(
            '<8sIIIIIIHHI', coff, 20
        )
        self.assertEqual(sec_name, b'.rsrc\x00\x00\x00')
        self.assertEqual(raw_off, 60)
        self.assertEqual(reloc_off, raw_off + raw_sz)
        self.assertEqual(nreloc, 8)  # 7 RT_ICON + 1 RT_GROUP_ICON
        self.assertEqual(sec_chars, ico_to_coff.IMAGE_SCN_CNT_INITIALIZED_DATA | ico_to_coff.IMAGE_SCN_MEM_READ)

        for i in range(nreloc):
            vaddr, sym_idx, rtype = struct.unpack_from('<IIH', coff, reloc_off + i * 10)
            self.assertEqual(sym_idx, 0)
            self.assertEqual(rtype, ico_to_coff.IMAGE_REL_AMD64_ADDR32NB)
            self.assertLess(vaddr, raw_sz)

    def test_cli_writes_file(self):
        with tempfile.TemporaryDirectory() as td:
            out = os.path.join(td, 'monado_icon.o')
            rc = ico_to_coff.main([self.ICO_PATH, out])
            self.assertEqual(rc, 0)
            self.assertTrue(os.path.isfile(out))
            self.assertGreater(os.path.getsize(out), 30000)

    def test_rc_and_embedded_header(self):
        with open(self.RC_PATH, 'r', encoding='utf-8') as f:
            self.assertIn('1 ICON "monado.ico"', f.read())
        with open(self.HEADER_PATH, 'r', encoding='utf-8') as f:
            hdr = f.read()
        self.assertIn('#define MONADO_WIN_ICON_ID 1', hdr)
        m = re.search(r'k_monado_icon_png\[\d+\]\s*=\s*\{\s*0x89,\s*0x50,\s*0x4e,\s*0x47', hdr)
        self.assertIsNotNone(m, 'embedded PNG signature missing from runtime/ui/monado_icon.h')


if __name__ == '__main__':
    unittest.main()
