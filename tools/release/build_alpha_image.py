#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (c) 2026 Jose Pilas
"""Build a clean, file-only Alpha from the verified M8 runtime. Never writes a card."""
import argparse,hashlib,json,os,re,shutil,struct,subprocess,sys,tempfile,zipfile
from pathlib import Path,PurePosixPath

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools/m8'))
from stock_format import inspect,BOOT_BYTES
from build_stock_candidate import assemble,prepare_growable_fat,sha,BOOT_FILES
VERSION='Alpha-0.0.1'
BASELINE='85120e975f25112136c938ae0afe9584628b1fd992e7084cb4207b7ff59eafc3'
DENIED=('.z64','.v64','.n64','.sfc','.smc','.gba','.gb','.gbc','.nes','.nds','.iso','.chd','.cue','.mpq','.wad','.sav','.srm','.state','.pdat')

def run(args):
    p=subprocess.run(list(map(str,args)),capture_output=True,text=True,timeout=600)
    if p.returncode:raise RuntimeError(str(args)+'\n'+p.stdout+p.stderr)
    return p.stdout+p.stderr

def relative(name):
    p=PurePosixPath(name)
    if not name or p.is_absolute() or '..' in p.parts or '\\' in name or str(p)!=name:raise ValueError('Unsafe relative path: '+name)
    return p

def rows(text):
    result=[];seen=set()
    for line in text.splitlines():
        r=line.split('\t')
        if len(r)!=4 or r[0] not in ('D','F','C','L') or not re.fullmatch('[0-7]{4}',r[1]):raise ValueError('Invalid manifest row')
        relative(r[2])
        if r[2] in seen:raise ValueError('Duplicate runtime path')
        seen.add(r[2])
        if r[0] in ('F','C'):relative(r[3])
        result.append(r)
    return result

def excluded(name):
    n=name.lower()
    return 'daedalus-lilsmoke' in n or 'mupen-lilsmoke' in n or n.startswith('usr/share/portsmoke/ports/sm64') or n=='usr/share/portsmoke/engines/sm64-byo-dev.peng'

def audit_package(path):
    with zipfile.ZipFile(path) as z:
        if z.testzip():raise ValueError('Broken package')
        for e in z.infolist():
            relative(e.filename.rstrip('/'))
            if Path(e.filename).suffix.lower() in DENIED:raise ValueError('Game/save asset in package: '+e.filename)
        meta=json.loads(z.read('meta.json'))
        if meta.get('id')!='org.lilsmoke.pcsx4all':raise ValueError('Unexpected builtin app')
    return meta

def config(text):
    values={'LILSMOKE_VERSION':VERSION,'SCAN_ROMS_ON_BOOT':'1','DEVICE_MODE':'tv',
      'DISPLAY_WIDTH':'1280','DISPLAY_HEIGHT':'720','TV_SAFE_AREA_PERCENT':'5','SHOW_BATTERY':'off',
      'EMULATOR_BACKEND':'retroarch','EMULATOR_snes':'od:pocketsnes',
      'EMULATOR_psx':'app:org.lilsmoke.pcsx4all','WIFI_ENABLED':'0','SSH_ENABLED':'0','USB_MODE':'off','BLUETOOTH_ENABLED':'off'}
    lines=[line for line in text.splitlines() if not any(re.match(r'^\s*'+re.escape(k)+r'\s*=',line) for k in values)]
    return '\n'.join(lines)+'\n'+''.join(k+'="'+v+'"\n' for k,v in values.items())

def sync_fat_info(path):
    with path.open('r+b') as f:
        b=f.read(512)
        reserved=struct.unpack_from('<H',b,14)[0];fat=struct.unpack_from('<I',b,36)[0]
        total=struct.unpack_from('<I',b,32)[0]
        clusters=(total-reserved-b[16]*fat)//b[13]
        f.seek(reserved*512+8);entries=f.read(clusters*4)
        if len(entries)!=clusters*4:raise ValueError('Truncated FAT table')
        free=0;next_free=0xffffffff
        for index,(value,) in enumerate(struct.iter_unpack('<I',entries),2):
            if value&0x0fffffff==0:
                free+=1
                if next_free==0xffffffff:next_free=index
        info=struct.unpack_from('<H',b,48)[0];backup=struct.unpack_from('<H',b,50)[0]
        f.seek(info*512);data=bytearray(f.read(512))
        struct.pack_into('<II',data,488,free,next_free)
        for sector in (info,backup+info):f.seek(sector*512);f.write(data)

def build(a):
    a.out=a.out.resolve()
    if a.out.exists() or a.out.suffix!='.img' or str(a.out).startswith('/dev/'):raise ValueError('Output must be a NEW image file')
    a.out.parent.mkdir(parents=True,exist_ok=True)
    if shutil.disk_usage(a.out.parent).free<5*1024**3:raise ValueError('At least 5 GiB free required')
    card=a.card.resolve(strict=True)
    if not (card/'lilsmoke/runtime.tsv').is_file() or not (card/'cubegm/icube.stock').is_file():raise ValueError('Expected read-only M8 card folder')
    original=(card/'lilsmoke/runtime.tsv').read_bytes()
    if hashlib.sha256(original).hexdigest()!=BASELINE:raise ValueError('Runtime baseline changed')
    source=inspect(a.stock_image)
    if source['boot_region_sha256']!='b0ec3c66e6b92742bbb8546ee8a30d4b622750c8af32f27ad93651c2d4724079':raise ValueError('Stock boot prefix mismatch')
    report={'version':VERSION,'device':'SEGAM-M8-V6.0','source_manifest_sha256':BASELINE,
       'physical_boot_tested':False,'physical_card_modified':False,'first_party_license':'MPL-2.0',
       'vendor_bsp_source_complete':False,'daedalus_included':False,'stock_files':{},'runtime_files':{},'removed':[]}
    with tempfile.TemporaryDirectory(prefix='lilsmoke-alpha-') as temporary:
        work=Path(temporary);os_tree=work/'os';rom=work/'rom';os_tree.mkdir();rom.mkdir()
        payload=os_tree/'lilsmoke';objects=payload/'files';objects.mkdir(parents=True)
        profile=json.loads((ROOT/'tools/device-adapter/profiles/segam-m8-v6.json').read_text())
        skip={'allfiles.lst','favorites.lst','setting.xml','Back_In_The_City.mp3','hcfota'}
        for name in profile['files']:
            relative(name)
            if name in skip or Path(name).suffix.lower() in ('.mp3','.wav','.zip') or 'bios' in name.lower():continue
            src=card/'cubegm'/name
            if name=='icube':src=card/'cubegm/icube.stock'
            if src.is_symlink():raise ValueError('Linked vendor dependency: '+name)
            if not src.is_file():
                if name in BOOT_FILES:raise ValueError('Missing vendor boot dependency: '+name)
                report.setdefault('absent_optional_stock_aliases',[]).append(name)
                continue
            dest=os_tree/'cubegm'/('icube.stock' if name=='icube' else name)
            dest.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(src,dest)
            report['stock_files'][str(dest.relative_to(os_tree))]=sha(dest)
        for name in ('icube',):
            shutil.copyfile(card/'cubegm'/name,os_tree/'cubegm'/name)
            report['stock_files']['cubegm/'+name]=sha(os_tree/'cubegm'/name)
        for name in ('m8-launch.sh','m8-probe','m8-grow','m8-boot-logo','libm8-boot-logo.so','libm8-display.so','boot-logo.argb'):
            shutil.copyfile(card/'lilsmoke'/name,payload/name)
            report['stock_files']['lilsmoke/'+name]=sha(payload/name)
        if report['stock_files'].get('cubegm/vmlinux.uImage')!='14cebc92fcd8d2fa8226bab4275e3d206fb72017cf574ddbff73a967d0ed56b5':raise ValueError('Working kernel mismatch')
        new=[];number=0
        def add(kind,mode,name,data):
            nonlocal number
            number+=1;obj=f'{number:06d}.bin';(objects/obj).write_bytes(data)
            new.append([kind,mode,name,obj]);report['runtime_files'][name]=hashlib.sha256(data).hexdigest()
        psx=ROOT/'build/pcsx4all-public-0.0.1/release-0.0.1/PCSX4ALL-LilSmoke-0.0.1.lilpck'
        if sha(psx)!='595072508f443e41f98b37b08cd542a2bf9c1bcb851f58024c4d5a84d473f54c':raise ValueError('Public PSX package mismatch')
        report['builtin_app']=audit_package(psx)
        for r in rows(original.decode('ascii')):
            kind,mode,name,obj=r
            if excluded(name):report['removed'].append(name);continue
            if Path(name).suffix.lower() in DENIED:raise ValueError('Unexpected game/save file in runtime: '+name)
            if kind not in ('F','C'):new.append(r);continue
            src=card/'lilsmoke/files'/obj
            if src.is_symlink() or src.stat().st_size>128*1024**2:raise ValueError('Unexpected runtime object')
            data=src.read_bytes()
            if name.endswith('.lilpck'):
                if 'PCSX4ALL-' not in name:raise ValueError('Unreviewed builtin package')
                data=psx.read_bytes();name='usr/share/lilsmoke/builtin-apps/PCSX4ALL-0.0.1.lilpck'
            elif name.endswith(('.peng','.opk','.7z','.zip')):raise ValueError('Unreviewed runtime archive: '+name)
            if name=='etc/lilsmoke/lilsmoke.conf.default':data=config(data.decode()).encode()
            if name=='etc/os-release':data=('NAME=LilSmokeOS\nID=lilsmoke\nVERSION='+VERSION+'\nVERSION_ID=0.0.1\nPRETTY_NAME="LilSmokeOS '+VERSION+'"\n').encode()
            if name=='etc/wpa_supplicant.conf':data=b'ctrl_interface=/run/wpa_supplicant\nupdate_config=0\n'
            if name=='etc/shadow' and any(line.split(':')[1] not in ('','!','!!','*') for line in data.decode().splitlines()):raise ValueError('Credential hash in runtime defaults')
            add(kind,mode,name,data)
        for doc in ('LICENSE','LICENSE_POLICY.md','THIRD_PARTY_NOTICES.md','CREDITS.md','README.md'):
            data=(ROOT/doc).read_bytes();(os_tree/doc).write_bytes(data)
            path='usr/share/licenses/lilsmoke-alpha/'+doc
            for directory in ('usr/share/licenses','usr/share/licenses/lilsmoke-alpha'):
                if not any(r[2]==directory for r in new):new.append(['D','0755',directory,''])
            add('F','0644',path,data)
        for entry in new:
            if entry[0] in ('F','C') and not (objects/entry[3]).is_file():raise ValueError('Incomplete closure')
        manifest=''.join('\t'.join(r)+'\n' for r in new)
        (payload/'runtime.tsv').write_text(manifest,encoding='ascii')
        report['manifest_sha256']=sha(payload/'runtime.tsv')
        (payload/'initial.conf').write_text(config((card/'lilsmoke/initial.conf').read_text()))
        os_mib,rom_mib=1024,768;start=2048+os_mib*2048
        (payload/'layout.conf').write_text(f'LayoutVersion=2\nBoard=segam-m8-v6-stock\nROMStart={start}\nExpandOnFirstBoot=yes\nBootAdapter=stock-preserved-experimental\n')
        persist=payload/'persist.ext4'
        with persist.open('wb') as f:f.truncate(64*1024**2)
        run(['mke2fs','-q','-t','ext4','-F','-m','0','-L','LILPERSIST',persist])
        swap=os_tree/'cubegm/pagefile.sys'
        with swap.open('wb') as f:f.truncate(128*1024**2)
        run(['mkswap','-f',swap])
        delivery=json.loads((ROOT/'tools/device-adapter/release/lilos/delivery.json').read_text())
        for name,entry in delivery['files'].items():
            relative(name);src=card/name
            if sha(src)!=entry['sha256']:raise ValueError('PC tool changed: '+name)
            dest=os_tree/name;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(src,dest)
        for directory in ('roms','bios','themes','packages','independent-apps','treefrog-ports','opendingux/apps'):
            (rom/directory).mkdir(parents=True,exist_ok=True)
        for system in ('nes','snes','gb','gbc','gba','md','sms','gg','pce','psx','n64','arcade','lynx','atari2600'):(rom/'roms'/system).mkdir()
        (rom/'README.txt').write_text('Place your own legally obtained ROMs in roms/<system>.\nInstall .lilpck applications through LilSmoke Explorer.\nOriginal TreeFrog ports: .sh plus its game folder in treefrog-ports.\nFirst boot expands LILROMS; do not disconnect power.\nNo games or BIOS files are included.\n')
        p1,p2=work/'lilos.fat',work/'lilroms.fat'
        with p1.open('wb') as f:f.truncate(os_mib*1024**2)
        run(['mkfs.vfat','-F','32','-S','512','-R','64','-h','2048','-n','LILOS',p1])
        # Normalize formatter track rounding inside its allocated FAT capacity.
        with p1.open('r+b') as f:
            boot=bytearray(f.read(512));struct.pack_into('<I',boot,32,os_mib*2048)
            backup=struct.unpack_from('<H',boot,50)[0]
            for sector in (0,backup):f.seek(sector*512);f.write(boot)
        report['fat_growth']=prepare_growable_fat(p2,rom_mib,start,131072)
        report['runtime_objects']=number
        (payload/'candidate.json').write_text(json.dumps({k:v for k,v in report.items() if k not in ('runtime_files',)},indent=2)+'\n')
        for directory in os_tree.iterdir():run(['mcopy','-s','-i',p1,directory,'::/'])
        for directory in rom.iterdir():run(['mcopy','-s','-i',p2,directory,'::/'])
        for fat in (p1,p2):
            sync_fat_info(fat)
            report[fat.stem+'_fsck']=run(['fsck.vfat','-n',fat])
        prefix=bytearray(source['prefix']);prefix[:512]=source['decoded_mbr'];prefix[446:510]=bytes(64)
        for i,(offset,count,active) in enumerate(((2048,os_mib*2048,128),(start,rom_mib*2048,0))):
            prefix[446+i*16:462+i*16]=struct.pack('<B3sB3sII',active,b'\xfe\xff\xff',0x0c,b'\xfe\xff\xff',offset,count)
        if (card/'lilsmoke/runtime.tsv').read_bytes()!=original:raise ValueError('Source card changed during snapshot')
        assemble(a.out,prefix,p1,p2)
        layout=inspect(a.out)
        if [p['label'] for p in layout['partitions']]!=['LILOS','LILROMS']:raise ValueError('Image partition verification failed')
        report['partitions']=layout['partitions'];report['image_bytes']=a.out.stat().st_size;report['image_sha256']=sha(a.out)
        report['passed']=True
        a.out.with_suffix('.img.json').write_text(json.dumps(report,indent=2)+'\n')
        a.out.with_suffix('.img.sha256').write_text(report['image_sha256']+'  '+a.out.name+'\n')
    print(json.dumps({k:report[k] for k in ('passed','image_bytes','image_sha256','runtime_objects','partitions')}))

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--card',type=Path,required=True);p.add_argument('--stock-image',type=Path,required=True);p.add_argument('--out',type=Path,required=True)
    build(p.parse_args())
