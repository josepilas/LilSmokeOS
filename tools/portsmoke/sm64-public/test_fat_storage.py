#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Transactions on a disposable regular FAT image; never a physical card."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
from unittest.mock import patch
import cook
from test_cook import executable


def main():
    with tempfile.TemporaryDirectory(prefix='sm64-fat-') as temporary:
        root=Path(temporary).resolve()
        image=root/'disposable.img'
        mount=root/'mounted'
        mount.mkdir()
        with image.open('xb') as stream: stream.truncate(64*1024*1024)
        assert image.is_file() and image.parent==root and mount.parent==root
        subprocess.run(['mkfs.vfat','-F','32','-n','SM64TEST',str(image)],check=True,capture_output=True)
        subprocess.run(['mount','-o','loop,fmask=0022,dmask=0022',str(image),str(mount)],check=True)
        try:
            app=mount/'Super Mario 64'
            original=app/'original'
            original.mkdir(parents=True)
            rom=original/'any name.z64'
            raw=b'\x80\x37\x12\x40'+b'\0'*(cook.ROM_BYTES-4)
            expected=hashlib.sha1(raw).hexdigest()
            rom.write_bytes(raw)
            actual_discover=cook.discover
            def discover(folder,cancel=lambda:False):
                return actual_discover(folder,cancel,expected=expected)
            def builder(stage,progress,cancel):
                (stage/'game').write_bytes(executable())
                return stage/'game',1024
            with patch.object(cook,'US_SHA1',expected),patch.object(cook,'discover',discover):
                cache=cook.Cache(app,'fat-test')
                game=cache.prepare(builder,lambda *args:None)
                assert not rom.exists() and game.exists() and os.access(game,os.X_OK)
                assert cache.current()==game
                assert cache.prepare(lambda *args:None,lambda *args:None)==game
            result=dict(passed=True,filesystem='FAT32',mount_modes='fmask=0022,dmask=0022',
                cache_commit_verified=True,restart_verified=True,app_original_removed=True,
                physical_card_accessed=False,gameplay_tested=False,synthetic_game_fixture=True)
            print(json.dumps(result,indent=2))
        finally:
            os.sync()
            subprocess.run(['umount',str(mount)],check=True)


if __name__=='__main__': main()
