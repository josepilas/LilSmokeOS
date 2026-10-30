"""Read-only parser/guard checks, never power off the host or modify a disk."""
from pathlib import Path
import subprocess
import tempfile
P=Path(__file__).resolve().parents[2]
B=Path('/root/lilsmoke-os/build/buildroot/output')
source=P/'board/segam-m8-v6-stock/m8-safe-power.c'
with tempfile.TemporaryDirectory(prefix='m8-safe-power-') as name:
    root=Path(name)
    host=root/'power-host'
    mips=root/'power-mips'
    for cc,binary in [('cc',host),(str(B/'host/bin/mipsel-buildroot-linux-musl-gcc'),mips)]:
        subprocess.run([cc,'-static','-Os','-std=c11','-Wall','-Wextra','-Werror',
            '-I'+str(B/'staging/usr/include/SDL'),str(source),
            str(source.with_name('m8-power-ui.c')),str(source.with_name('m8-native-video.c')),'-o',str(binary)],check=True)
        prefix=[] if binary==host else ['qemu-mipsel','-cpu','74Kf']
        for title,contents,status in [
            ('readonly','/dev/mmcblk0p1 /mnt/sdcard vfat ro,nosuid 0 0\n/dev/mmcblk0p2 /storage vfat ro,nodev 0 0\n',0),
            ('writable','/dev/mmcblk0p1 /mnt/sdcard vfat rw,nosuid 0 0\n',75),
            ('writable-remount-ro','/dev/mmcblk0p1 /mnt/sdcard vfat rw,relatime,errors=remount-ro 0 0\n',75),
            ('writable-contains-ro','/dev/mmcblk0p2 /storage vfat rw,iocharset=iso8859-1,errors=remount-ro 0 0\n',75),
            ('readonly-remount-ro','/dev/mmcblk0p1 /lib vfat ro,errors=remount-ro 0 0\n',0),
            ('alias','/dev/mmcblk0p2 /tmp/root/storage vfat rw 0 0\n',75),
            ('spaces','/dev/mmcblk0p2 /media/My\\040Card vfat ro 0 0\n',0),
            ('other','tmpfs /tmp tmpfs rw 0 0\n',0),
        ]:
            table=root/(title+'.mounts');table.write_text(contents)
            result=subprocess.run([*prefix,str(binary),'--check-table',str(table)],capture_output=True,text=True)
            assert result.returncode==status,(binary,title,result.stdout,result.stderr)
        assert subprocess.run([*prefix,str(binary),'--prepare'],capture_output=True).returncode==75
        assert subprocess.run([*prefix,str(binary),'--shutdown'],capture_output=True).returncode==75
        unit=root/('unit-'+binary.name)
        subprocess.run([cc,'-static','-Os','-std=c11','-Wall','-Wextra','-Werror',
            '-I'+str(B/'staging/usr/include/SDL'),str(P/'tools/m8/test_safe_power_unit.c'),'-o',str(unit)],check=True)
        subprocess.run([*prefix,str(unit)],check=True)
        ui=root/('ui-'+binary.name)
        subprocess.run([cc,'-static','-Os','-std=c11','-Wall','-Wextra','-Werror',
            str(P/'tools/m8/test_power_ui.c'),str(source.with_name('m8-power-ui.c')),'-o',str(ui)],check=True)
        subprocess.run([*prefix,str(ui)],check=True)
    subprocess.run([str(B/'host/bin/mipsel-buildroot-linux-musl-gcc'),'-static','-Os','-std=c11',
        '-Wall','-Wextra','-Werror','-I'+str(B/'staging/usr/include/SDL'),str(source),
        str(source.with_name('m8-power-ui.c')),str(source.with_name('m8-native-video.c')),
        '-o',str(B/'target/usr/lib/lilsmoke/m8-safe-power')],check=True)
print('PASS host/MIPS readonly detection, writable rejection, aliases, escaped paths and non-M8 hardware refusal')
