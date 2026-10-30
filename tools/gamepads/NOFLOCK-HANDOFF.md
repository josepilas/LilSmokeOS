# M8 no-flock controller daemon

The vendor kernel returns MIPS ENOSYS (89) for flock. The old startup treated
this as another live owner and exited before opening XInput/PS controllers.

## Implementation

- `package/lilsmoke-usb-gamepads/src/owner-lock.h`: no advisory locking. Private
  persistent `<state>.lock.d` contains atomic mkdir PID/start-time claims and
  immutable bakery tickets. Concurrent contenders choose one owner. Only dead
  or PID-reused claims are reclaimed; live/stopped owners never expire by age.
  Proc identity and filesystem operations use anchored descriptors. Symlinks,
  insecure directories, malformed records and ticket overflow fail closed.
- `package/lilsmoke-usb-gamepads/src/usb-gamepads.c`: acquire/release helper,
  anchored atomic state publication, and opt-in fixture hotplug. Normal polling,
  SDL/raw USB behavior, and the ABI-1 424-byte state layout remain unchanged.
- `package/lilsmoke-usb-gamepads/src/lilsmoke-input-diagnostics`: failed persist
  logging uses the existing session-log helper, then /dev/null. Dmesg includes
  FAT/MMC/read-only/remount/I/O errors. No repair or mount operation is added.
- `tools/gamepads/{test_owner_lock.c,deny_advisory_locks.c,test_noflock.py}`:
  focused host and real-MIPS acceptance; both advisory APIs forced ENOSYS,
  concurrent startup, SIGKILL/recovery, cleanup, path bounds, X/PS hotplug,
  daemon generations, unchanged frozen legacy ABI readers, diagnostics fallback.

The empty `.lock.d` container intentionally persists after shutdown. Do not
delete it while the service is running. The old `.lock` regular file is unused
and harmless. Stop the previous daemon before upgrading, since old flock-based
binaries do not participate in the new owner protocol.

## Build And Deploy (Parent Only)

Acceptance runner (serial; no SDK/target/card installation):

```sh
python3 tools/gamepads/test_noflock.py --output /root/usb-gamepads-noflock-20261018
```

Accepted output is `/root/usb-gamepads-noflock-20261018/acceptance.json`.
The stripped MIPS binary is in that same directory as `lilsmoke-usb-gamepads`.
The runner links the already-built SDK SDL2; it never rebuilds SDL or consumers.

Install just these two payloads into the parent's candidate/rootfs staging:

```text
/root/usb-gamepads-noflock-20261018/lilsmoke-usb-gamepads
    -> /usr/bin/lilsmoke-usb-gamepads                       mode 0755
package/lilsmoke-usb-gamepads/src/lilsmoke-input-diagnostics
    -> /usr/bin/lilsmoke-input-diagnostics                  mode 0755
```

For a parent production package rebuild, refresh the local-source package build
directory with BOTH `usb-gamepads.c` and the new `owner-lock.h` (and retain
`rk707.h`), then run `make -j1 lilsmoke-usb-gamepads-rebuild` with the parent's
normal Buildroot configuration. Alternatively use a clean re-extraction of this
single package. A rebuild without refreshing its local-source copy can compile
old code or miss the new header. The diagnostics install recipe already takes
the script directly from the external package source.

No SDL2 rebuild/reinstall, UI, PocketSNES, RetroArch, odSDL, PCSX4all or SM64
rebuild is required for this correction. Preserve cached SM64 and the parent's
S07 logger. The state ABI and consumer headers did not change in this task.
No SDK target, launcher, boot/logo, physical USB forwarding or SD writes were
performed. Physical M8 validation and final candidate/card integration belong
to the parent.
