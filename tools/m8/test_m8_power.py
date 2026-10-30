"""Power-off QA in an ephemeral chroot containing a shell and fake commands.

Run as root in Ubuntu WSL. Never execute the real launcher or host poweroff.
Hardware poweroff is never available in this fixture.
"""
import os
from pathlib import Path
import shutil
import stat
import subprocess
import tempfile
import unittest

PROJECT = Path(__file__).resolve().parents[2]
HELPER = PROJECT / "package/lilsmoke-scripts/src/lilsmoke-power"
LAUNCHER = PROJECT / "board/segam-m8-v6-stock/m8-launch.sh"
UI = PROJECT / "package/lilsmoke-ui/src/src"

FAKE_COMMAND = r'''
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/stat.h>
#include <errno.h>
#include <unistd.h>
int main(int argc, char **argv) {
    const char *name = strrchr(argv[0], '/'); name = name ? name + 1 : argv[0];
    FILE *log = fopen("/events", "a"); if (!log) return 90;
    fprintf(log, "%s", name);
    for (int i = 1; i < argc; ++i) fprintf(log, " %s", argv[i]);
    fputc('\n', log); fclose(log);
    if (!strcmp(name, "mkdir")) {
        if (!mkdir(argv[argc-1], 0700)) return 0;
        return argc > 2 && !strcmp(argv[1], "-p") && errno == EEXIST ? 0 : 1;
    }
    if (!strcmp(name, "cat")) {
        FILE *f = fopen(argv[argc-1], "r"); if (!f) return 1;
        int c; while ((c = fgetc(f)) != EOF) putchar(c);
        fclose(f); return 0;
    }
    if (!strcmp(name, "sync") && getenv("FAKE_EXPORT_DURING_SYNC")) {
        FILE *f = fopen("/run/lilsmoke-storage/exporting", "w"); if (!f) return 91; fclose(f);
    }
    if (!strcmp(name, "sync") && getenv("FAKE_SYNC_FAIL")) return 1;
    if (!strcmp(name, "mv")) {
        if (getenv("FAKE_MV_FAIL")) return 1;
        return argc == 3 && !rename(argv[1], argv[2]) ? 0 : 1;
    }
    if (!strcmp(name, "rm")) { if (argc > 1) unlink(argv[argc-1]); return 0; }
    if (!strcmp(name, "rmdir")) { if (argc > 1) rmdir(argv[argc-1]); return 0; }
    if (!strcmp(name,"chmod")) return argc==3 ? chmod(argv[2],(mode_t)strtoul(argv[1],NULL,8)) : 1;
    if (!strcmp(name, "umount") && getenv("FAKE_UMOUNT_FAIL")) return 1;
    if (!strcmp(name, "poweroff") && getenv("FAKE_POWEROFF_FAIL")) return 1;
    if (!strcmp(name, "chroot")) {
        if (getenv("FAKE_CHILD_FAIL")) return 1;
        if (argc==4 && !strcmp(argv[2],"/bin/cat")) {
            FILE *file=fopen("/fake","rb");if(!file)return 1;
            unsigned char buffer[4096];size_t n;
            while((n=fread(buffer,1,sizeof(buffer),file)))fwrite(buffer,1,n,stdout);
            fclose(file);
        }
        return 0;
    }
    if (!strcmp(name,"lilsmoke-safe-power")) {
        if(argc==2&&!strcmp(argv[1],"--self-check")) {puts("M8_POWER_GUARD version=2");return 0;}
        return getenv("FAKE_GUARD_FAIL") ? 75 : 0;
    }
    /* End only this fixture's parent shell, including the intentional final sleep. */
    if (!strcmp(name, "sleep")) { kill(getppid(), SIGTERM); return 0; }
    return 0;
}
'''


class PowerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not hasattr(os, "chroot") or os.geteuid() != 0:
            raise unittest.SkipTest("requires root Linux/WSL for isolated chroot")
        cls.temp = tempfile.TemporaryDirectory(prefix="lilsmoke-power-", dir="/tmp")
        cls.base = Path(cls.temp.name)
        source = cls.base / "fake.c"
        source.write_text(FAKE_COMMAND)
        subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-static",
                        str(source), "-o", str(cls.base / "fake")], check=True)
        cls.helper = HELPER.read_text()
        cls.launcher = LAUNCHER.read_text()
        # Only execute the exact cleanup/fallback functions and application loop.
        # Startup/probe/growth/mount code is deliberately never executed.
        cls.cleanup = cls.launcher.split("cleanup() {", 1)[1].split("\ntrap cleanup EXIT", 1)[0]
        cls.fallback = cls.launcher.split("fallback() {", 1)[1].split("\n[ ! -f", 1)[0]
        cls.loop = cls.launcher.split("echo NativeUIStarting\nsync\n", 1)[1]

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def setUp(self):
        self.root = Path(tempfile.mkdtemp(prefix="case-", dir=self.base))
        for path in ("bin", "sbin", "lib/x86_64-linux-gnu", "lib64", "run/lilsmoke-storage",
                     "run/lilsmoke-usb", "tmp", "dev", "lock", "sandbox/run", "sandbox/dev/shm"):
            (self.root / path).mkdir(parents=True, exist_ok=True)
        # No busybox binary/applets are installed in the jail.
        shell = os.environ.get("LILSMOKE_POWER_QA_SHELL", "dash")
        if shell not in ("dash", "busybox"):
            raise ValueError("unsupported QA shell")
        for source, target in (("/bin/" + shell, "bin/sh"),
                               ("/lib/x86_64-linux-gnu/libc.so.6", "lib/x86_64-linux-gnu/libc.so.6"),
                               ("/lib64/ld-linux-x86-64.so.2", "lib64/ld-linux-x86-64.so.2")):
            shutil.copyfile(source, self.root / target)
            (self.root / target).chmod(0o755)
        if shell == "busybox":
            shutil.copyfile("/lib/x86_64-linux-gnu/libresolv.so.2",
                            self.root / "lib/x86_64-linux-gnu/libresolv.so.2")
        shutil.copyfile(self.base / "fake", self.root / "fake")
        for name in ("sync", "mv", "umount", "rm", "rmdir", "sleep", "chroot", "mkdir", "cat", "chmod", "swapoff"):
            shutil.copyfile(self.base / "fake", self.root / "bin" / name)
            (self.root / "bin" / name).chmod(0o755)
        for name in ("sbin/poweroff", "stock"):
            shutil.copyfile(self.base / "fake", self.root / name)
            (self.root / name).chmod(0o755)
        (self.root / "dev/console").touch()  # Regular fixture files, not devices.
        (self.root / "dev/null").touch()
        (self.root / "lock/owner").write_text("fixture\n")

    def run_shell(self, source, args=(), **extra):
        (self.root / "test.sh").write_text(source)
        root = str(self.root)

        def enter_jail():
            os.chroot(root)
            os.chdir("/")

        result = subprocess.run(["/bin/sh", "/test.sh", *args],
                                env={"PATH": "/bin:/sbin", **extra},
                                preexec_fn=enter_jail, start_new_session=True,
                                capture_output=True, text=True, timeout=15)
        trace = self.root / "events"
        return result, trace.read_text().splitlines() if trace.exists() else []

    def run_supervisor(self, marker=None, **extra):
        if marker is not None:
            (self.root / "sandbox/run/lilsmoke-poweroff-request").write_text(marker)
        script = ("set -u\nROOT=/sandbox\nLOCK=/lock\nSTOCK=/stock\n"
                  # Startup is excluded above; initialize its optional logo
                  # state explicitly before exercising the supervisor loop.
                  "BASE=/base\nloading_disabled=yes\nlogo_minimum=0\nloading_dir=/loading\n"
                  "cut() { printf '0\\n'; }\n"
                  # BusyBox ash has a sleep builtin. Terminate only this fixture
                  # at the loop delay rather than accidentally taking a real nap.
                  "sleep() { exit 143; }\n"
                  "child=''\nlogo_child=''\nmounted_root=yes\nshutting_down=no\ncleanup() {" + self.cleanup +
                  "\nfallback() {" + self.fallback + "\ntrap cleanup EXIT\n" + self.loop)
        result, trace = self.run_shell(script, **extra)
        self.assertNotIn("parameter not set", result.stderr)
        self.assertNotIn("unbound variable", result.stderr)
        return result, trace

    def test_argument_refusal_has_no_side_effects(self):
        for args in ((), ("reboot",), ("poweroff", "extra")):
            result, trace = self.run_shell(self.helper, args)
            self.assertEqual(result.returncode, 64)
            self.assertEqual(trace, [])

    def test_usb_export_refuses_before_sync_or_request(self):
        (self.root / "run/lilsmoke-storage/exporting").touch()
        result, trace = self.run_shell(self.helper, ("poweroff",), LILSMOKE_STOCK_SUPERVISOR="1")
        self.assertEqual(result.returncode, 75)
        self.assertIn("refused", result.stderr)
        self.assertNotIn("sync", trace)
        self.assertFalse((self.root / "run/lilsmoke-poweroff-request").exists())

    def test_stock_request_is_atomic_private_and_never_calls_poweroff(self):
        result, trace = self.run_shell(self.helper, ("poweroff",), LILSMOKE_STOCK_SUPERVISOR="1")
        self.assertEqual(result.returncode, 0)
        self.assertEqual(trace, ["mkdir -p /run/lilsmoke-usb", "mkdir /run/lilsmoke-usb/lock",
                                 "sync", "mv /run/lilsmoke-poweroff-request.new /run/lilsmoke-poweroff-request"])
        marker = self.root / "run/lilsmoke-poweroff-request"
        self.assertEqual(marker.read_text(), "poweroff\n")
        self.assertEqual(marker.stat().st_mode & 0o777, 0o600)
        self.assertFalse(marker.with_suffix(".new").exists())

    def test_normal_init_path_syncs_then_fake_poweroff(self):
        result, trace = self.run_shell(self.helper, ("poweroff",))
        self.assertEqual(result.returncode, 0)
        self.assertEqual(trace, ["mkdir -p /run/lilsmoke-usb", "mkdir /run/lilsmoke-usb/lock", "sync", "poweroff"])

    def test_unprepared_stock_shutdown_refuses_without_side_effects(self):
        result, trace = self.run_shell(self.helper, ("poweroff",),
                                      LILSMOKE_STOCK_SUPERVISOR="1", LILSMOKE_M8_SAFE_POWEROFF="0")
        self.assertEqual(result.returncode, 75)
        self.assertEqual(trace, [])

    def test_sync_or_rename_failure_does_not_publish_request(self):
        for flag in ("FAKE_SYNC_FAIL", "FAKE_MV_FAIL"):
            result, trace = self.run_shell(self.helper, ("poweroff",),
                                          LILSMOKE_STOCK_SUPERVISOR="1", **{flag: "1"})
            self.assertNotEqual(result.returncode, 0)
            self.assertFalse((self.root / "run/lilsmoke-poweroff-request").exists())
            self.assertNotIn("poweroff", trace)

    def test_shutdown_releases_mounts_then_execs_ram_guard(self):
        _, trace = self.run_supervisor("poweroff\n")
        unmounts = [line for line in trace if line.startswith("umount ")]
        expected = ["umount /sandbox/" + part for part in
                    ("run/m8-loading", "dev/pts", "dev/shm", "persist", "storage", "lilos", "os", "dev", "sys", "proc")]
        self.assertEqual(unmounts, expected + ["umount /sandbox"])
        self.assertLess(trace.index("sync"), trace.index(expected[0]))
        self.assertLess(trace.index("umount /sandbox"), trace.index("lilsmoke-safe-power --shutdown"))
        self.assertNotIn("stock", trace)
        self.assertNotIn("poweroff", trace)
        self.assertLess(self.loop.index("exec >/dev/null 2>&1"), self.loop.index("exec /tmp/lilsmoke-safe-power --shutdown"))
        self.assertNotIn("exec >/dev/console", self.launcher)
        self.assertEqual((self.root / "tmp/lilsmoke-m8-shutdown").read_text(), "poweroff\n")

    def test_shutdown_without_console_still_executes_ram_guard(self):
        (self.root / "dev/console").unlink()
        _, trace = self.run_supervisor("poweroff\n")
        self.assertIn("lilsmoke-safe-power --shutdown", trace)
        self.assertIn("umount /sandbox", trace)
        self.assertNotIn("stock", trace)
        self.assertNotIn("poweroff", trace)

    def test_unopenable_console_does_not_abort_shutdown(self):
        console = self.root / "dev/console"
        console.unlink()
        os.mknod(console, stat.S_IFCHR | 0o600, os.makedev(5, 0))
        # Give the console path a real tty node with no controlling terminal.
        # Opening it fails with ENXIO, exercising the same fatal redirection as
        # the M8's ENODEV. A regular file (the old fixture) cannot catch this bug.
        probe, probe_trace = self.run_shell("exec >/dev/console 2>&1\nsync\n")
        self.assertNotEqual(probe.returncode, 0)
        self.assertEqual(probe_trace, [])
        self.assertTrue("No such device" in probe.stderr or "No such device or address" in probe.stderr,
                        probe.stderr)
        _, trace = self.run_supervisor("poweroff\n")
        self.assertIn("lilsmoke-safe-power --shutdown", trace)
        self.assertIn("umount /sandbox", trace)
        self.assertNotIn("stock", trace)
        self.assertNotIn("poweroff", trace)

    def test_failed_terminal_guard_never_relaunches_game_or_stock(self):
        _, trace = self.run_supervisor("poweroff\n", FAKE_GUARD_FAIL="1")
        self.assertEqual(trace.count("lilsmoke-safe-power --shutdown"), 1)
        self.assertNotIn("poweroff",trace)
        self.assertNotIn("stock", trace)
        self.assertEqual(sum(line.startswith("chroot ") and "lilsmoke-ui" in line for line in trace), 1)

    def test_storage_guard_failure_never_claims_shutdown(self):
        _, trace = self.run_supervisor("poweroff\n", FAKE_GUARD_FAIL="1")
        self.assertNotIn("poweroff",trace)
        self.assertIn("lilsmoke-safe-power --shutdown",trace)
        self.assertNotIn("stock",trace)

    def test_ui_confirmation_and_destructor_order_static_contract(self):
        settings = (UI / "SettingsView.cpp").read_text()
        header = (UI / "SettingsView.h").read_text()
        main = (UI / "main.cpp").read_text()
        self.assertIn("confirmPowerOff_ = false;", settings)
        self.assertIn("if (evt != InputEvent::Pressed) return nullptr;", settings)
        self.assertIn("if (confirmPowerOff_ && onPowerOff_) onPowerOff_();", settings)
        self.assertIn("&& !pendingPowerOff_", header)
        callback = main.split("sv->setOnPowerOff", 1)[1].split("});", 1)[0]
        self.assertLess(callback.index("config_.save()"), callback.index("requestProcessLaunch"))
        self.assertIn('{"lilsmoke-power", "poweroff"}', callback)
        tail = main.split("int main(int argc", 1)[1]
        self.assertLess(tail.index("launch = lilsmoke::takeProcessLaunch()"),
                        tail.index("lilsmoke::executeProcessLaunch"))
        self.assertIn("\n    }\n", tail)

    def test_usb_export_starting_during_sync_must_refuse(self):
        result, _ = self.run_shell(self.helper, ("poweroff",),
                                  LILSMOKE_STOCK_SUPERVISOR="1", FAKE_EXPORT_DURING_SYNC="1")
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse((self.root / "run/lilsmoke-poweroff-request").exists())

    def test_malformed_marker_must_not_poweroff(self):
        _, trace = self.run_supervisor("not-a-poweroff-request\n")
        self.assertNotIn("poweroff", trace)

    def test_refused_helper_must_not_fall_back_to_stock(self):
        _, trace = self.run_supervisor(None, FAKE_CHILD_FAIL="1")
        self.assertNotIn("stock", trace)


if __name__ == "__main__":
    unittest.main(verbosity=2)
