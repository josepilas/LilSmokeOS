"""Verify the public PC-tool payload before putting it in LILOS, not the RAM root."""
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import stat
import subprocess


def verify_delivery(directory):
    root = Path(directory).absolute()
    def plain(path, directory=False):
        info = path.lstat()
        if stat.S_ISLNK(info.st_mode) or getattr(info,'st_file_attributes',0) & getattr(stat,'FILE_ATTRIBUTE_REPARSE_POINT',0):
            raise ValueError('Links are not PC-tool payload files')
        if not (stat.S_ISDIR if directory else stat.S_ISREG)(info.st_mode):
            raise ValueError('Unexpected PC-tool file type')
        return info
    for path in (root,*root.parents): plain(path,True)
    manifest_path = root/'delivery.json'
    if plain(manifest_path).st_size > 256*1024: raise ValueError('Oversized PC-tool metadata')
    report = json.loads(manifest_path.read_text())
    files = report.get('files',{})
    if report.get('proprietary_firmware_included') is not False or not isinstance(files,dict) or not 1 <= len(files) <= 64:
        raise ValueError('Not a bounded public PC-tool delivery')
    total = 0
    seen = set()
    for name,row in files.items():
        parts = PurePosixPath(name).parts
        if '\\' in name or not name.startswith('PC-Tools/LilSmoke Device Adapter/') or any(p in ('','..','.') for p in parts) or name.casefold() in seen:
            raise ValueError('Unsafe PC-tool member')
        seen.add(name.casefold())
        path = root
        for part in parts[:-1]:
            path /= part
            plain(path,True)
        path /= parts[-1]
        before = plain(path)
        if not 0 < before.st_size <= 64*1024*1024 or before.st_size != row['bytes']:
            raise ValueError('PC-tool size mismatch')
        digest = hashlib.sha256()
        with path.open('rb') as stream:
            for chunk in iter(lambda:stream.read(1024*1024),b''): digest.update(chunk)
        after = plain(path)
        if before.st_size != after.st_size or before.st_mtime_ns != after.st_mtime_ns or digest.hexdigest() != row['sha256']:
            raise ValueError('PC-tool content mismatch: '+name)
        total += before.st_size
    if total > 64*1024*1024 or total != report['bytes'] or len(files) != report['file_count']:
        raise ValueError('PC-tool delivery budget mismatch')
    actual = set()
    for base,dirs,names in os.walk(root/'PC-Tools',followlinks=False):
        for name in dirs: plain(Path(base)/name,True)
        for name in names:
            plain(Path(base)/name)
            actual.add((Path(base)/name).relative_to(root).as_posix())
    if actual != set(files): raise ValueError('Unexpected/unlisted PC-tool content')
    return root,report


def copy_to_fat(directory, fat_spec):
    root,report = verify_delivery(directory)
    subprocess.run(['mcopy','-s','-i',str(fat_spec),str(root/'PC-Tools'),'::/'],check=True)
    return dict(files=report['file_count'],bytes=report['bytes'],
                proprietary_firmware_included=False,location='LILOS/PC-Tools/LilSmoke Device Adapter')
