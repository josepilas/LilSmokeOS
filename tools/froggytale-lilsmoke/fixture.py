#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
"""Original synthetic WAD16: one room, creation bytecode, and a four-pixel atlas.

This is a format regression fixture written from the pinned parser schema,
not GameMaker/Undertale data and not evidence of Undertale compatibility.
"""
from pathlib import Path
import struct
import zlib


def make(out):
    out = Path(out)
    out.mkdir(parents=True, exist_ok=False)
    blob = bytearray(b'FORM\0\0\0\0')

    def u32(*v):
        return struct.pack('<'+'I'*len(v), *(x & 0xffffffff for x in v))

    def chunk(name, data):
        start = len(blob)+8
        blob.extend(name.encode()+u32(len(data))+data)
        return start

    gen = chunk('GEN8', bytes(136))
    strings = ['LilSmoke synthetic fixture', 'room_fixture', 'gml_RoomCreationCode_fixture']
    start = len(blob)+8
    table = bytearray(u32(len(strings))+bytes(len(strings)*4))
    pointers = []
    for i, text in enumerate(strings):
        value = text.encode()
        pointers.append(start+len(table)+4)
        struct.pack_into('<I', table, 4+i*4, start+len(table))
        table.extend(u32(len(value))+value+b'\0')
    table.extend(bytes((-len(table))%4))
    chunk('STRG', table)
    blob[gen:gen+4] = bytes((1,16,0,0))
    for offset, value in ((4,pointers[0]),(8,pointers[0]),(20,1),(40,pointers[0]),
            (44,1),(48,4),(52,1767),(60,320),(64,240),(100,pointers[0]),(128,1),(132,0)):
        struct.pack_into('<I',blob,gen+offset,value)
    start = len(blob)+8
    empty = start+8+88
    room = u32(1,start+8)+u32(pointers[1],pointers[1],320,240,30,0,0x403020,1,0,0,
                            empty,empty,empty,empty,0,0,0,320,240,0,0,0)+u32(0)
    chunk('ROOM',room)
    start=len(blob)+8
    bytecode=u32(0x84000013,0x84000017,0x0c220000,0x9c000000)
    chunk('CODE',u32(1,start+8)+u32(pointers[2],len(bytecode),0,8,0)+bytecode)
    start=len(blob)+8
    # source/content/stored dimensions are 2x2; no copyrighted texture pages.
    chunk('TPAG',u32(1,start+8)+struct.pack('<11H',0,0,2,2,0,0,2,2,2,2,0)+bytes(2))
    def png_chunk(tag,data):
        return struct.pack('>I',len(data))+tag+data+struct.pack('>I',zlib.crc32(tag+data))
    scan=bytes((0,255,0,0,255,0,255,0,255,0,0,0,255,255,255,255,255,0))
    png=b'\x89PNG\r\n\x1a\n'+png_chunk(b'IHDR',struct.pack('>IIBBBBB',2,2,8,6,0,0,0))+png_chunk(b'IDAT',zlib.compress(scan))+png_chunk(b'IEND',b'')
    start=len(blob)+8
    chunk('TXTR',u32(1,start+8,0,start+16)+png)
    struct.pack_into('<I',blob,4,len(blob)-8)
    (out/'game.unx').write_bytes(blob)
    record=struct.pack('<8HIHHHHI',2,2,2,2,0,0,2,2,2,2,2,0,0,0)
    (out/'ATLAS.BIN').write_bytes(struct.pack('<4sIIIII8x',b'ATLS',1,1,1,320,1)+record)
    # byte pair rb,ga; red, green, blue, transparent white.
    (out/'ATLAS.RGBA4444').write_bytes(bytes((0,0,0xf0,0x0f,0,0xff,0x0f,0x0f,0xff,0xf0)))
    return out


if __name__ == '__main__':
    import argparse
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--out',type=Path,required=True)
    print(make(p.parse_args().out))
