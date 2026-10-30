import hashlib
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
from pc_tools_payload import verify_delivery, copy_to_fat


class DeliveryTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.name = 'PC-Tools/LilSmoke Device Adapter/launch.py'
        self.file = self.root/self.name
        self.file.parent.mkdir(parents=True)
        self.file.write_bytes(b'print("synthetic public tool")\n')
        data = self.file.read_bytes()
        self.report = dict(files={self.name:dict(bytes=len(data),sha256=hashlib.sha256(data).hexdigest())},
            bytes=len(data),file_count=1,proprietary_firmware_included=False)
        self.save()
    def tearDown(self): self.temp.cleanup()
    def save(self): (self.root/'delivery.json').write_text(json.dumps(self.report))
    def test_verified_copy_uses_argument_vector_only(self):
        self.assertEqual(verify_delivery(self.root)[1],self.report)
        with patch('pc_tools_payload.subprocess.run') as run:
            result = copy_to_fat(self.root,'synthetic.img@@1048576')
        self.assertEqual(result['files'],1)
        self.assertEqual(run.call_args.args[0],['mcopy','-s','-i','synthetic.img@@1048576',str(self.root/'PC-Tools'),'::/'])
    def test_tampered_content(self):
        self.file.write_bytes(b'corrupt')
        with self.assertRaises(ValueError): verify_delivery(self.root)
    def test_unlisted_file(self):
        self.file.with_name('extra.bin').write_bytes(b'extra')
        with self.assertRaises(ValueError): verify_delivery(self.root)
    def test_private_firmware_flag(self):
        self.report['proprietary_firmware_included']=True; self.save()
        with self.assertRaises(ValueError): verify_delivery(self.root)
    def test_traversal(self):
        self.report['files']={'PC-Tools/LilSmoke Device Adapter/../../bad':next(iter(self.report['files'].values()))}; self.save()
        with self.assertRaises(ValueError): verify_delivery(self.root)
    def test_budget(self):
        self.report['bytes']=64*1024*1024+1; self.save()
        with self.assertRaises(ValueError): verify_delivery(self.root)
    def test_link(self):
        other=self.root/'outside'; other.write_bytes(self.file.read_bytes())
        self.file.unlink()
        try: self.file.symlink_to(other)
        except OSError: self.skipTest('Host lacks symlink privilege')
        with self.assertRaises(ValueError): verify_delivery(self.root)


if __name__ == '__main__': unittest.main()
