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

import json

# Tracks (.TRK) and cars (CAR<ID>.RES).
tracks = sorted(
    os.path.splitext(f)[0].upper()
    for f in os.listdir(STUNTS)
    if f.lower().endswith(".trk")
)
cars = sorted(
    f[3:-4].upper()
    for f in os.listdir(STUNTS)
    if f.upper().startswith("CAR") and f.upper().endswith(".RES")
)

# One bundle per track (default car) and per track x car combo, each
# auto-booting straight into a race via /t<TRACK> [/c<CAR>] — a convenience;
# the in-game menu/track selection works too.
for t in tracks:
    pack(f"web/track-{t.lower()}.jsdos", f"REST2.EXE /t{t}")
    for c in cars:
        pack(f"web/track-{t.lower()}-{c.lower()}.jsdos", f"REST2.EXE /t{t} /c{c}")

# One bundle per replay (.RPL), auto-playing it (a bare NAME arg).
replays = sorted(
    os.path.splitext(f)[0].upper()
    for f in os.listdir(STUNTS)
    if f.lower().endswith(".rpl")
)
for r in replays:
    pack(f"web/replay-{r.lower()}.jsdos", f"REST2.EXE {r}")

with open("web/tracks.json", "w") as fh:
    json.dump(tracks, fh)
with open("web/cars.json", "w") as fh:
    json.dump(cars, fh)
with open("web/replays.json", "w") as fh:
    json.dump(replays, fh)
print("tracks:", tracks)
print("cars:", cars)
print("replays:", replays)
