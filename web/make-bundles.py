#!/usr/bin/env python3
"""Build js-dos bundles for the restunts2 web demo.

Usage: make-bundles.py [stunts-dir]
Packs the game data dir plus REST2.EXE into one .jsdos per test case.
"""
import os
import sys
import zipfile

STUNTS = sys.argv[1] if len(sys.argv) > 1 else "/Users/joris/projects/restunts/stunts"
SKIP = {".DS_Store"}

CONF = """[dosbox]
machine=svga_s3
memsize=16
[cpu]
core=auto
cputype=auto
cycles=fixed 20000
[dos]
xms=true
ems=true
[autoexec]
echo off
mount c .
c:
%s
"""

# name -> DOS command line to auto-boot
CASES = {
    "fun": "REST2.EXE FUN",
    "fun-jagu": "REST2.EXE FUN /cJAGU",
    "fun-vett": "REST2.EXE FUN /cVETT",
    "fun-p962": "REST2.EXE FUN /cP962",
    "default": "REST2.EXE DEFAULT",
    "race-jagu-zct5": "REST2.EXE /tZCT5 /cJAGU",
    "race-lm02": "REST2.EXE /cLM02",
    "plain": "REST2.EXE",
}

def pack(path, runline):
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as z:
        for f in sorted(os.listdir(STUNTS)):
            if f in SKIP:
                continue
            p = os.path.join(STUNTS, f)
            if os.path.isfile(p):
                z.write(p, f)
        z.writestr(".jsdos/dosbox.conf", CONF % runline)
    print(f"{path} ({os.path.getsize(path) // 1024}K): {runline}")


os.makedirs("web", exist_ok=True)
for name, runline in CASES.items():
    pack(f"web/{name}.jsdos", runline)

# One bundle per track file, auto-booting a race on it (bypasses the
# in-game track preview, which hangs in the ported build). Also emit
# tracks.json so the page can list them.
import json
tracks = sorted(
    os.path.splitext(f)[0].upper()
    for f in os.listdir(STUNTS)
    if f.lower().endswith(".trk")
)
for t in tracks:
    pack(f"web/track-{t.lower()}.jsdos", f"REST2.EXE /t{t}")
with open("web/tracks.json", "w") as fh:
    json.dump(tracks, fh)
print("tracks:", tracks)
