# Frozen Runtime Notices

The Windows executable includes unmodified CPython 3.14.4, Tcl/Tk 8.6.15,
PyInstaller 6.21.0 bootloader/runtime hooks, and their required runtime libraries.
The accompanying license files retain their upstream copyright notices.

- `Python-LICENSE.txt`: PSF and historical Python terms, plus the bundled
  libffi, bzip2, Zstandard/Apache and Microsoft Windows runtime notices supplied
  by this Python installation. Windows runtime code is for Microsoft platforms
  only; no Microsoft trademark endorsement is implied.
- `Tcl-LICENSE.txt` and `Tk-LICENSE.txt`: Tcl/Tk copyright and permission terms.
- `PyInstaller-COPYING.txt`: GPL terms with the bootloader exception and
  Apache-licensed runtime-hook terms. PyInstaller itself was not modified.
- `OpenSSL-LICENSE.txt`: Apache-2.0 terms for the bundled OpenSSL 3.0.19 libraries.
- `zlib-ng-LICENSE.md`: zlib-compatible compression runtime copyright/license.

Python/Tk/libffi/OpenSSL libraries and the PyInstaller bootloader are used only
by the PC utility. Nothing here becomes a dependency of LilSmoke's MIPS runtime.
The portable Linux/macOS scripts use the user's installed Python/Tk, rather than
redistributing Microsoft Windows runtime binaries onto those platforms.

Upstream sources: [Python](https://www.python.org/),
[Tcl](https://github.com/tcltk/tcl/blob/core-8-6-15/license.terms),
[PyInstaller](https://pyinstaller.org/en/stable/license.html),
[OpenSSL](https://github.com/openssl/openssl/blob/openssl-3.0.19/LICENSE.txt),
[zlib-ng](https://github.com/zlib-ng/zlib-ng/blob/develop/LICENSE.md).

The adapter's own source is MPL-2.0-licensed in `../LICENSE`. Public hash profiles
contain no vendor executable/firmware bytes. Private stock exports are supplied
by their owner and must not be redistributed with this tool.
