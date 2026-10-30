# Preparation And Cache Protocol

With no explicit WAD argument, the engine searches the installed app's
`original/data.win`, `original/game.unx` and `original/game.ios`. Only trusted
GameMaker WAD16 bytecode is admitted by this port, not EXE/DLL/YYC/GMRT code.

The native preparation UI uses the included public-domain font, not a game
font or copyrighted atlas. It displays stage, percentage and input/output
byte counts. The textual diagnostic record has this existing engine format:

```text
UNDERTALE_PREP progress=N stage=TEXT input_bytes=N total_bytes=N atlas_bytes=N
UNDERTALE_PREP cache=created source_sha1=HEX
UNDERTALE_PREP cache=hit validation=sealed-metadata source_sha1=HEX
```

These diagnostic strings are not a general application IPC protocol. The
generation directory is `cache/<source-sha1>-v1/`. Completion metadata and
the atomic staging/rename boundary prevent an interrupted conversion from
being reused as complete. Preparation locks track PID/start time/boot identity
and do not require the unsupported kernel flock operation on this target.

`cache/active.txt` uses `Undertale-LilSmoke sealed-metadata-v2` and records
source/cache metadata. It is a local performance seal, not an adversarial
integrity/security guarantee. FAT remount changes to inode/device identity
are handled by the qualified metadata path. `UNDERTALE_VERIFY_CACHE=1`
requests full validation/resealing rather than the quick path.

Keep originals/music and saves backed up. Do not edit a complete cache in
place, share derived ATLAS files, or treat cache metadata as an authorization
to redistribute the game's data. Use `FROGGY_CACHE_DIR`/`FROGGY_SAVE_DIR` to
choose alternative writable locations. No proprietary input or cache is in
the repository, release package, source archive or public QA evidence.
