#!/usr/bin/env python3
"""Production launcher control flow with filesystem-only path relocation."""
import os
from pathlib import Path
import subprocess
import tempfile

P=Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix='media-launcher-') as directory:
    root=Path(directory)
    for name in ('usr/bin','usr/lib/lilsmoke','persist/logs','run','media'):
        (root/name).mkdir(parents=True,exist_ok=True)
    text=(P/'package/lilsmoke-media/lilsmoke-media-open').read_text()
    for name in ('/usr/','/persist/','/run/'):
        text=text.replace(name,str(root)+name)
    script=root/'launcher'
    script.write_text(text)
    def executable(path,text):
        path.write_text('#!/bin/sh\n'+text+'\n');path.chmod(0o755)
    executable(root/'usr/lib/lilsmoke/lilsmoke-config-read','echo 50')
    executable(root/'usr/bin/lilsmoke-media',
        'printf "%s\\n%s\\n" "${LILSMOKE_UI_WINDOWED-unset}" "$1" >"$QA_MEDIA"; exit "${QA_STATUS:-0}"')
    executable(root/'usr/bin/lilsmoke-ui','printf "%s\\n" "${LILSMOKE_UI_WINDOWED-unset}" >"$QA_UI"')
    executable(root/'usr/lib/lilsmoke/lilsmoke-power','printf "%s\\n" "$1" >"$QA_POWER"')
    media=root/'media/file with spaces.mp3';media.touch()
    base=os.environ.copy()
    base.pop('LILSMOKE_UI_WINDOWED',None)
    base.update(QA_MEDIA=str(root/'played'),QA_UI=str(root/'returned'),QA_POWER=str(root/'power'))
    for native in ('0','1'):
        for window in (None,'preserved'):
            env=base|{'LILSMOKE_HCFB':native}
            if window is not None:env['LILSMOKE_UI_WINDOWED']=window
            subprocess.run(['sh',str(script),str(media),str(media.parent)],check=True,env=env)
            expected='1' if native=='1' else window or 'unset'
            assert (root/'played').read_text().splitlines()==[expected,str(media)]
            assert (root/'returned').read_text().strip()==(window or 'unset')
    (root/'returned').unlink()
    subprocess.run(['sh',str(script),str(media),str(media.parent)],check=True,env=base|{'QA_STATUS':'76'})
    assert (root/'power').read_text()=='poweroff\n' and not (root/'returned').exists()
    (root/'run/lilsmoke-storage').mkdir()
    (root/'run/lilsmoke-storage/exporting').touch()
    assert subprocess.run(['sh',str(script),str(media),str(media.parent)],env=base).returncode==2
print('PASS launcher: quoted paths, media-only window mode, Explorer return, poweroff handoff, export guard')
