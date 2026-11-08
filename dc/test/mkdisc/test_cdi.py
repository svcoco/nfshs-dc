"""Regression tests for the disc-wide CDI descriptor, independent of track offsets."""
import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('mkcdi', Path(__file__).parents[2]/'tools/mkdisc/mkcdi.py')
mkcdi = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mkcdi)

class CdiTests(unittest.TestCase):
    def test_global_extent_and_label_for_different_filename_lengths(self):
        with tempfile.TemporaryDirectory(dir=Path(__file__).parent) as temp:
            root = Path(temp)
            iso = bytearray(320*2048)
            signature = b'SEGA SEGAKATANA SEGA ENTERPRISES'
            iso[:len(signature)] = signature
            iso[32768:32775] = b'\x01CD001\x01'
            iso[32808:32840] = b'NFSHS_TEST'.ljust(32,b' ')
            struct.pack_into('<I',iso,32768+158,11722)
            src = root/'test.iso'
            src.write_bytes(iso)
            for name in ('a.cdi','disc.cdi','nfshs-720p.cdi'):
                out = root/name
                mkcdi.build(src,out)
                data = out.read_bytes()
                length = struct.unpack_from('<I',data,len(data)-4)[0]
                trailer = data[-length:]
                mark = bytes.fromhex('000001000000ffffffff')*2
                last = trailer.rfind(mark)
                end = last+25+trailer[last+24]
                self.assertEqual(struct.unpack_from('<I',trailer,end+31)[0],11702+150+320)
                self.assertEqual(trailer[end+35],32)
                self.assertEqual(trailer[end+36:end+68],b'NFSHS_TEST'.ljust(32,b' '))
                self.assertNotIn(b'240pSuiteDC',trailer)
                self.assertEqual(len(data)-length,452*2352+470*2048)

    def test_invalid_sector_rejected_before_output(self):
        with self.assertRaises(ValueError):
            mkcdi.build('does-not-exist.iso','unused.cdi',2352)

if __name__ == '__main__':
    unittest.main()
