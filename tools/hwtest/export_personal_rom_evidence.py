#!/usr/bin/env python3
"""Export real-time clips from VM recordings; never interpolate or retime."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile

p = argparse.ArgumentParser()
p.add_argument("source", type=Path)
p.add_argument("destination", type=Path)
p.add_argument("--clip", action="append", required=True,
               help="capture stem:seconds before capture:duration, or start:0:duration")
p.add_argument("--image", action="append", default=[], help="capture stem:output label")
a = p.parse_args()
source = a.source.resolve()
destination = a.destination.resolve()
destination.mkdir(parents=True, exist_ok=True)
metadata = json.loads((source/"manifest.json").read_text())
if metadata.get("diagnostic_input_probe"):
    p.error("input diagnostics are not game emulation evidence")
events = json.loads((source/"input-events.json").read_text()) if (source/"input-events.json").exists() else []
manual = source/"manual-events.ndjson"
if manual.exists():
    events += [json.loads(line) for line in manual.read_text().splitlines() if line]
recorded = metadata["recording_started_wall_seconds"]
captures = {}
for event in events:
    action = event.get("action", event.get("command", {}))
    if action.get("execute") == "screendump":
        captures[Path(action["arguments"]["filename"]).stem] = event["wall_seconds"]-recorded
raw = source/"raw-video.mkv"
probe = json.loads(subprocess.check_output(
    ["ffprobe", "-v", "error", "-show_format", "-of", "json", str(raw)], text=True))
duration = probe['format'].get('duration')
if duration is None:
    # A host crash can leave an FFV1 capture without a finalized duration.
    packets = subprocess.check_output(['ffprobe','-v','error','-select_streams','v:0',
        '-show_entries','packet=pts_time,duration_time','-of','csv=p=0',str(raw)],text=True)
    ends=[]
    for line in packets.splitlines():
        try:
            values=line.split(','); ends.append(float(values[0])+(float(values[1]) if len(values)>1 and values[1]!='N/A' else 1/15))
        except ValueError: continue
    if not ends: raise ValueError('capture contains no timestamped video packets')
    length=max(ends)
else: length=float(duration)
segments = []
with tempfile.TemporaryDirectory(prefix="lilsmoke-evidence-") as temporary:
    temporary = Path(temporary)
    for index, specification in enumerate(a.clip):
        name, before, duration = specification.split(":")
        if name != "start" and name not in captures:
            captures[name] = (source/(name+".ppm")).stat().st_mtime-recorded
        start = 0 if name == "start" else max(0, captures[name]-float(before))
        duration = min(float(duration), length-start)
        if duration <= 0:
            raise ValueError("clip outside recording: "+specification)
        part = temporary/f"part-{index:02}.webm"
        subprocess.run(["ffmpeg", "-y", "-nostdin", "-hide_banner", "-loglevel", "error",
            "-ss", str(start), "-i", str(raw), "-t", str(duration), "-an",
            "-c:v", "libvpx-vp9", "-deadline", "realtime", "-cpu-used", "8",
            "-threads", "2", "-crf", "32", "-b:v", "0", str(part)], check=True)
        segments.append(dict(capture=name, source_start_seconds=start, duration_seconds=duration))
    listing = temporary/"segments.txt"
    listing.write_text("".join(f"file '{temporary/f'part-{i:02}.webm'}'\n" for i in range(len(segments))))
    video = destination/"ui-game-test.webm"
    subprocess.run(["ffmpeg", "-y", "-nostdin", "-hide_banner", "-loglevel", "error",
        "-f", "concat", "-safe", "0", "-i", str(listing), "-c", "copy", str(video)], check=True)
for specification in a.image:
    name, label = specification.split(":")
    if Path(label).name != label or Path(name).name != name:
        p.error("image names must not contain paths")
    original = source/(name+".png")
    target = destination/(label+".png")
    if original.exists():
        shutil.copyfile(original, target)
    else:
        subprocess.run(["convert", str(source/(name+".ppm")), str(target)], check=True)
for name in ("manifest.json", "input-events.json", "manual-events.ndjson", "serial.log"):
    if (source/name).exists():
        shutil.copyfile(source/name, destination/name)
logs = source/"persist/logs"
for name in ("lilsmoke-ui.log", "retroarch.log", "pcsx4all.log", "pocketsnes.log"):
    if (logs/name).exists():
        shutil.copyfile(logs/name, destination/name)
export = dict(segments=segments, editing="Waiting sections removed; all retained footage at original speed.",
              source_duration_seconds=length, audio="Silent capture; guest ALSA enabled.",
              limitations=metadata["note"], hashes={})
for file in destination.iterdir():
    if file.suffix in (".webm", ".png"):
        export["hashes"][file.name] = hashlib.sha256(file.read_bytes()).hexdigest()
(destination/"evidence.json").write_text(json.dumps(export, indent=2))
print("EVIDENCE_EXPORT_OK", destination, flush=True)
