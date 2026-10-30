# 0.0.1

First public Undertale-LilSmoke source/package release by Jose Pilas, based
on FroggyTale and Butterscotch. Bring your own legally obtained Undertale
1.08/WAD16 data.win and external OGGs; no game or Flowey assets are included.

The working engine is preserved exactly. Its internal diagnostics/preparation
banner still say 0.1.0; the public package and release tag are 0.0.1.

Includes on-device progress/cancellation, private atlas-cache reuse, native
input and incremental SDL audio, plus matching offline corresponding source
with SDL2/libatomic dependencies and retained licenses. Runtime/derived code
is AGPL-3.0-only; only new independent release/audit tools are MPL-2.0.

Keep the corresponding-source ZIP with the binary when redistributing. The
`.lilpck` does not embed a large source archive. Install with LilSmoke's app
installer, then supply `original/data.win` and your OGG files. Back up saves;
uninstall may remove app-local originals, caches and saves. This public
number is not an automatic downgrade/upgrade for private 0.1.0 installations.

Evidence includes the unchanged engine's retained synthetic and one-CPU
128 MiB MIPS/FAT32/remount/audio smoke qualification. Public packaging QA and
an offline source build are additional checks, not a new gameplay test.
No full playthrough, general game compatibility, full-speed, physical
speaker/HDMI/latency or save/load gameplay qualification is claimed.
Video and Box2D remain disabled; atlas filtering is approximate.

Credits: Jose Pilas; Synaps33/FroggyTale; mrpowergamerbr/Butterscotch and all
upstream/vendor contributors. Undertale belongs to Toby Fox and its creators;
this project is unofficial and supplies none of their proprietary content.
