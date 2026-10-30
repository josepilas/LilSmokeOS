# SPDX-License-Identifier: MPL-2.0
"""Synthetic analysis fixtures, not executable gameplay qualification."""
import ctypes
import io
import json
from pathlib import Path
import struct
import unittest
import zipfile

ROOT=Path(__file__).resolve().parents[1]
API=['retro_init','retro_deinit','retro_api_version','retro_set_environment','retro_set_video_refresh','retro_set_audio_sample','retro_set_audio_sample_batch','retro_set_input_poll','retro_set_input_state','retro_get_system_info','retro_get_system_av_info','retro_set_controller_port_device','retro_reset','retro_run','retro_serialize_size','retro_serialize','retro_unserialize','retro_load_game','retro_unload_game','retro_get_region','retro_get_memory_data','retro_get_memory_size']
def elf(libs=('libc.so.6',),api=True,machine=8,flags=0x70001007):
    strings=bytearray(b'\0'); offsets={}
    for s in [*libs,*(API if api else [])]:offsets[s]=len(strings);strings+=s.encode()+b'\0'
    symbols=b'\0'*16+b''.join(struct.pack('<IIIBBH',offsets[s],0,0,0x12,0,1) for s in(API if api else []))
    shoff=116; dataoff=shoff+160; st=dataoff; dy=st+len(strings); sy=dy+(len(libs)+5)*8
    dynamic=b''.join(struct.pack('<II',1,offsets[s]) for s in libs)+struct.pack('<IIIIIIIIII',5,st,10,len(strings),6,sy,11,16,0,0)
    b=bytearray(sy+len(symbols));b[:7]=b'\x7fELF\1\1\1'
    struct.pack_into('<HHIIIIIHHHHHH',b,16,3,machine,1,0,52,shoff,flags,52,32,2,40,4,0)
    struct.pack_into('<IIIIIIII',b,52,1,0,0,0,len(b),len(b),5,4096)
    struct.pack_into('<IIIIIIII',b,84,2,dy,dy,0,len(dynamic),len(dynamic),4,4)
    struct.pack_into('<IIIIIIIIII',b,shoff+40,0,3,0,0,st,len(strings),0,0,1,0)
    struct.pack_into('<IIIIIIIIII',b,shoff+80,0,6,0,0,dy,len(dynamic),1,0,4,8)
    struct.pack_into('<IIIIIIIIII',b,shoff+120,0,11,0,0,sy,len(symbols),1,0,4,16)
    b[st:dy]=strings;b[dy:sy]=dynamic;b[sy:]=symbols;return bytes(b)
def archive(files):
    stream=io.BytesIO()
    with zipfile.ZipFile(stream,'w',zipfile.ZIP_DEFLATED) as z:
        for name,data in files.items():z.writestr(name,data)
    return stream.getvalue()
def port(**change):
    m=dict(schema_version=1,id='test',name='Test Game',launch=dict(type='libretro',binary='core.so',content='Data.rsdk'),required_file_sets=[['Data.rsdk']])
    m.update(change)
    return {'Test.sh':b'#!/bin/sh\nexec /mnt/sdcard/roms/ports/test/run.sh\n','test/config.json':json.dumps(m).encode(),'test/run.sh':b'#!/bin/sh\nset -eu\nexec /mnt/sdcard/cubegm/picoarch "$PORT_DIR/core.so" "$PORT_DIR/Data.rsdk"\n','test/core.so':elf(),'test/Data.rsdk':b'private synthetic asset\0'*1024,'test/.requires_game_data':b'1','test/NOTICE.md':b'upstream notice'}

class CoreTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.c=ctypes.CDLL(str(ROOT/'_build/libtfconvert.so'))
        for name in ('tf_open','tf_runtime'):getattr(cls.c,name).argtypes=[ctypes.c_void_p,ctypes.c_size_t]
        cls.c.tf_report.restype=cls.c.tf_error.restype=ctypes.c_char_p
        cls.c.tf_convert.argtypes=[ctypes.c_uint,ctypes.c_char_p,ctypes.c_char_p]
        cls.c.tf_output_data.restype=ctypes.c_void_p;cls.c.tf_output_size.restype=ctypes.c_size_t
    def setUp(self):self.buffers=[]
    def tearDown(self):self.c.tf_close()
    def open(self,files):
        data=archive(files);b=ctypes.create_string_buffer(data);self.buffers.append(b)
        return self.c.tf_open(b,len(data))
    def runtime(self,files=None):
        data=archive(files or {'glibc/lilretro':elf(api=False),'licenses/runtime.txt':b'Original test-only frontend license'})
        b=ctypes.create_string_buffer(data);self.buffers.append(b);return self.c.tf_runtime(b,len(data))
    def report(self):return json.loads(self.c.tf_report())
    def test_real_plan_and_bounded_runtime_selection(self):
        files=port();self.assertEqual(self.open(files),1);self.assertFalse(self.report()['ports'][0]['ready'])
        self.assertEqual(self.runtime(),1);self.assertTrue(self.report()['ports'][0]['ready'])
        self.assertEqual(self.c.tf_convert(0,b'Converted Game',b'0.0.1'),1)
        data=ctypes.string_at(self.c.tf_output_data(),self.c.tf_output_size())
        with zipfile.ZipFile(io.BytesIO(data)) as z:
            self.assertIsNone(z.testzip());self.assertEqual(z.read('runtime/port/Data.rsdk'),files['test/Data.rsdk'])
            self.assertEqual(z.read('runtime/port/core.so'),files['test/core.so'])
            self.assertEqual([s for s in z.namelist() if '/'not in s and s.endswith('.sh')],['test.sh'])
            self.assertIn(b'--application',z.read('test.sh'));self.assertNotIn(b'picoarch',z.read('test.sh'))
            self.assertEqual(json.loads(z.read('meta.json'))['name'],'Converted Game')
            self.assertFalse(json.loads(z.read('runtime/launch.json'))['hardware_rendering'])
            self.assertTrue(json.loads(z.read('adapter.json'))['independent_libretro'])
    def test_transitive_dependency_closure_not_just_direct(self):
        p=port();p['test/core.so']=elf(('libcustom.so', 'libc.so.6'));self.open(p)
        self.runtime({'glibc/lilretro':elf(api=False),'glibc/lib/libcustom.so':elf(('libsecond.so','libc.so.6'),False),'glibc/lib/libsecond.so':elf(api=False),'glibc/lib/unused.so':elf(api=False)})
        r=self.report()['ports'][0];self.assertTrue(r['ready']);self.assertIn('libsecond.so',r['dependencies'])
        self.assertEqual(self.c.tf_convert(0,b'Game',b'1.0'),1)
        with zipfile.ZipFile(io.BytesIO(ctypes.string_at(self.c.tf_output_data(),self.c.tf_output_size())))as z:
            self.assertIn('runtime/lib/libsecond.so',z.namelist());self.assertNotIn('runtime/lib/unused.so',z.namelist())
    def test_wrong_abi_gpu_api_and_missing_data_blocked(self):
        for binary in [elf(machine=40),elf(flags=0x70001207),elf(('libGL.so.1','libc.so.6')),elf(api=False)]:
            f=port();f['test/core.so']=binary;self.assertEqual(self.open(f),1);self.assertFalse(self.report()['ports'][0]['ready']);self.c.tf_close()
        f=port();del f['test/Data.rsdk'];self.open(f);self.assertFalse(self.report()['ports'][0]['ready'])
    def test_unsafe_names_duplicates_and_links_rejected(self):
        for bad in ['../escape','/absolute','a\\b','CON','foo.','a/../b','a//b','file:stream']:
            f=port();f[bad]=b'x';self.assertEqual(self.open(f),0,bad);self.c.tf_close()
        f=port();f['test/CORE.SO']=b'x';self.assertEqual(self.open(f),0)
        f=port();f['test/Data.rsdk/child']=b'x';self.assertEqual(self.open(f),0)
    def test_setup_side_effects_not_silently_dropped(self):
        f=port();f['test/run.sh']=b'#!/bin/sh\ncurl http://bad.invalid/x\n';self.open(f);self.assertFalse(self.report()['ports'][0]['ready'])
    def test_missing_and_mixed_dependencies_blocked(self):
        f=port();f['test/core.so']=elf(('libabsent.so','libc.so.6'));self.open(f);self.runtime();self.assertFalse(self.report()['ports'][0]['ready'])
        self.assertEqual(self.c.tf_convert(0,b'X',b'1'),0)
    def test_invalid_metadata_budget(self):
        f=port();f['test/config.json']=b'{"schema_version":1,"id":"a","id":"b"}';self.assertEqual(self.open(f),0)
        f=port();f['test/config.json']=b'['*40+b']'*40;self.assertEqual(self.open(f),0)
    def test_bad_name_and_selection(self):
        self.open(port());self.runtime()
        for name,version in [(b'../evil',b'1'),(b'CON',b'1'),(b'X',b'v1'),(b'X',b'1.2.3.4')]:self.assertEqual(self.c.tf_convert(0,name,version),0)
        self.assertEqual(self.c.tf_convert(8,b'X',b'1'),0)
    def test_alternate_engine_route_updates_typed_metadata(self):
        f=port();f['test/core.so']=elf(machine=40);f['test/core_software_libretro.so']=elf()
        self.assertEqual(self.open(f),1);self.runtime();r=self.report()['ports'][0]
        self.assertTrue(r['ready']);self.assertEqual(r['binary'],'core_software_libretro.so')
        self.assertEqual(self.c.tf_convert(0,b'Fallback',b'1'),1)
        with zipfile.ZipFile(io.BytesIO(ctypes.string_at(self.c.tf_output_data(),self.c.tf_output_size())))as z:
            self.assertEqual(json.loads(z.read('runtime/port/config.json'))['launch']['binary'],'core_software_libretro.so')
            self.assertEqual(json.loads(z.read('runtime/upstream/config.original.json'))['launch']['binary'],'core.so')
    def test_missing_primary_can_use_compatible_alternative(self):
        f=port();del f['test/core.so'];f['test/core_mips_libretro.so']=elf()
        self.open(f);self.runtime();self.assertTrue(self.report()['ports'][0]['ready'])

    def test_native_setup_is_preserved_not_matched_to_a_template(self):
        f=port();m=json.loads(f['test/config.json']);m['launch']=dict(type='native',binary='game.mipsel');m['required_file_sets']=[]
        f['test/config.json']=json.dumps(m).encode();del f['test/core.so'];f['test/game.mipsel']=elf(api=False)
        script=b'#!/bin/sh\nexport HOME="$PORT_DIR/home"\nmkdir -p "$HOME"\n. ./saved-options.txt\nfor item in one two; do echo "$item" >>log.txt; done\n./game.mipsel >>log.txt 2>&1\n'
        f['test/run.sh']=script;f['test/saved-options.txt']=b'volume=1\n'
        self.assertEqual(self.open(f),1);self.assertTrue(self.report()['ports'][0]['ready'])
        self.assertEqual(self.c.tf_convert(0,b'Native',b'0.2.0'),1)
        with zipfile.ZipFile(io.BytesIO(ctypes.string_at(self.c.tf_output_data(),self.c.tf_output_size())))as z:self.assertEqual(z.read('runtime/port/run.sh'),script)

    def test_metadata_free_arbitrary_native_launcher_inference(self):
        native=bytearray(elf(api=False));struct.pack_into('<H',native,16,2)
        f={'novel_port/Launch Anything.sh':b'#!/bin/sh\ncd "$(dirname "$0")"\nexec ./totally-new.mipsel\n','novel_port/totally-new.mipsel':bytes(native),'novel_port/data.txt':b'example'}
        self.assertEqual(self.open(f),1,self.c.tf_error());r=self.report()['ports'][0]
        self.assertTrue(r['ready']);self.assertEqual(r['discovery'],'ELF and launcher inference');self.assertEqual(r['id'],'novel_port')
        self.assertEqual(self.c.tf_convert(0,b'Novel Port',b'0.2.0'),1)
        with zipfile.ZipFile(io.BytesIO(ctypes.string_at(self.c.tf_output_data(),self.c.tf_output_size())))as z:
            self.assertEqual(json.loads(z.read('runtime/port/config.json'))['launch']['binary'],'totally-new.mipsel')
            self.assertIn(b'Launch Anything.sh',z.read('runtime/port/run.sh'))
            self.assertEqual(z.read('runtime/port/Launch Anything.sh'),f['novel_port/Launch Anything.sh'])

    def test_windows_native_launcher_normalization_keeps_original(self):
        f=port();m=json.loads(f['test/config.json']);m['launch']=dict(type='native',binary='core.so');m['required_file_sets']=[]
        f['test/config.json']=json.dumps(m).encode();f['test/core.so']=elf(api=False)
        original=b'\xef\xbb\xbf#!/bin/sh\r\nexec ./core.so\r\n';f['test/run.sh']=original
        self.assertEqual(self.open(f),1);self.assertTrue(self.report()['ports'][0]['ready'])
        self.assertEqual(self.c.tf_convert(0,b'Native',b'0.2.0'),1)
        with zipfile.ZipFile(io.BytesIO(ctypes.string_at(self.c.tf_output_data(),self.c.tf_output_size())))as z:
            self.assertEqual(z.read('runtime/port/run.sh'),b'#!/bin/sh\nexec ./core.so\n')
            self.assertEqual(z.read('runtime/upstream/launcher.original.sh'),original)

    def test_native_helper_dependencies_and_architecture_are_checked(self):
        f=port();m=json.loads(f['test/config.json']);m['launch']=dict(type='native',binary='game.mipsel');m['required_file_sets']=[]
        f['test/config.json']=json.dumps(m).encode();f['test/game.mipsel']=elf(api=False);del f['test/core.so']
        f['test/run.sh']=b'#!/bin/sh\n./picker.mipsel\nexec ./game.mipsel\n';f['test/picker.mipsel']=elf(('libpicker.so','libc.so.6'),api=False)
        self.open(f);self.assertFalse(self.report()['ports'][0]['ready']);self.assertIn('libpicker.so',self.report()['ports'][0]['missing_dependencies'])
        self.runtime({'glibc/lib/libpicker.so':elf(api=False)})
        self.assertTrue(self.report()['ports'][0]['ready']);self.assertEqual(self.report()['ports'][0]['helper_executables'],1)
        f['test/picker.mipsel']=elf(api=False,machine=40);self.open(f);self.assertFalse(self.report()['ports'][0]['ready'])
if __name__=='__main__':unittest.main(verbosity=2)
