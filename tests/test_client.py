import importlib.util
from pathlib import Path
import struct
import unittest
import zlib
import tempfile
from PIL import Image

spec=importlib.util.spec_from_file_location("paper",Path(__file__).parents[1]/"tools/paper.py")
paper=importlib.util.module_from_spec(spec);spec.loader.exec_module(paper)

class ClientTests(unittest.TestCase):
    def test_wire_layout(self):
        body=bytes(range(256))*15+b"t"*64
        chunks=list(paper.packets(body,2,0x12345678))
        self.assertTrue(all(len(p)<=240 for p in chunks))
        joined=bytearray(len(body))
        for p in chunks:
            magic,version,kind,request,total,offset,crc=struct.unpack("<2sBBIHHI",p[:16])
            self.assertEqual((magic,version,kind,request,total,crc),(b"WP",1,2,0x12345678,len(body),zlib.crc32(body)))
            joined[offset:offset+len(p)-16]=p[16:]
        self.assertEqual(bytes(joined),body)

    def test_padding_and_polarity(self):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/"test.png";im=Image.new("RGB",(250,122),"white")
            im.putpixel((0,0),(0,0,0));im.putpixel((249,121),(0,0,0));im.save(p)
            frame=paper.image_frame(p)
            self.assertEqual(len(frame),3904)
            self.assertEqual(frame[0],0x80)
            self.assertEqual(frame[-1],0x40)
            self.assertEqual(sum(b.bit_count() for b in frame),2)

    def test_reject_wrong_frame_size(self):
        with self.assertRaises(ValueError):list(paper.packets(b"bad",2))

if __name__=="__main__":unittest.main()
