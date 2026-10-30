# LilSmoke Explorer

LilSmoke Explorer is the built-in file manager. It is inspired by the
351Files handheld workflow: one focused file list, controller-first
navigation, and a simple action menu instead of a desktop-style window
manager.

## Features

- Browse `/storage`, `/persist`, `/`, and any mounted media.
- Open folders, text files, image files, and archive listings.
- Mark one or more files with `Y`.
- Copy, move, paste, rename, and delete files or folders.
- Create folders and empty files.
- View file properties, including recursive directory size.
- View images supported by SDL2_image: PNG, JPEG, WebP, BMP.
- View and lightly edit text/config files up to 256 KB.
- Extract archives:
  - `.7z`
  - `.zip`
  - `.rar`
  - `.tar`, `.tar.gz`, `.tgz`
  - `.tar.bz2`, `.tbz`, `.tbz2`
  - `.tar.xz`, `.txz`
  - other formats supported by `7za`, such as `.cpio`, `.rpm`, `.deb`,
    `.cab`, and many single-file compressed streams.

Archive extraction writes to storage-backed directories, not RAM. This
keeps the 128-256 MB hardware budget safe when users unpack larger
archives.

## Controls

| Button | Browser action |
|---|---|
| D-pad | Move cursor |
| A / Start | Open |
| B / Left | Back / parent folder |
| X | Action menu |
| Y | Mark / unmark |
| L/R | Page up / page down |
| Select | Find by name |

The action menu contains context-aware commands: open, select, copy,
move, paste, rename, delete, new folder, new file, extract, text edit,
properties, refresh, and quick jumps.

## Text Entry

LilSmoke Explorer includes a tiny controller text prompt:

| Button | Text prompt action |
|---|---|
| Left / Right | Move cursor |
| Up / Down | Change current character |
| X | Insert space |
| Y | Delete character |
| A / Start | Accept |
| B | Cancel |

It is not fast like a keyboard, but it keeps rename/new-file/search
usable without adding a large on-screen keyboard dependency.

## Archive Tools

The default defconfigs include:

- `p7zip` with `7za` for 7z, ZIP, TAR-family archives, and other common
  formats.
- `unrar` for RAR archives.
- BusyBox `tar`, `gzip`, `bzip2`, `xz`, and `unzip` as lightweight
  fallbacks.

If extraction fails, the command log is written to:

```sh
/tmp/lilsmoke-explorer-archive.log
```

## Limits

- Text editing is line-based and capped at 256 KB per file.
- There is no background copy queue; file operations run synchronously.
- Destructive operations require confirmation, but the explorer can
  browse the full filesystem. Be careful outside `/storage` and
  `/persist`.
