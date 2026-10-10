# RP2 file format

`.RP2` is the *enhanced* replay format written by the port's autosave. It is
**byte-for-byte a standard `.RPL`** followed by a small timing trailer, so the
original game can still play it (see [Backwards compatibility](#backwards-compatibility)).

Every finished race is written twice by `run_game` in `src/c/replayboot.c`:

- `lastrep.rpl` — the plain standard replay (no trailer),
- `lastrep.rp2` — the replay plus the trailer described here.

## Layout

```
+--------------------------------+------------------------------+
|  standard .RPL                 |  .RP2 trailer                |
|  (26-byte header + input block)|  (16 bytes)                  |
+--------------------------------+------------------------------+
```

### Standard `.RPL` part

See `file_write_replay` (`src/asm/seg005.asm:1872`) and `file_load_replay`
(`src/asm/seg005.asm:1829`). The file length is `recordedframes + 0x724` bytes:

- **Header (first 26 bytes = 13 words)** — an exact copy of the `GAMEINFO`
  struct (`src/c/externs.h:136`), copied with `rep movsw`:

  | Offset | Size | Field                                  |
  | ---    | ---  | ---                                    |
  | 0      | 4    | `game_playercarid` (e.g. `JAGU`)       |
  | 4      | 1    | `game_playermaterial`                  |
  | 5      | 1    | `game_playertransmission`              |
  | 6      | 1    | `game_opponenttype`                    |
  | 7      | 4    | `game_opponentcarid`                   |
  | 11     | 1    | `game_opponentmaterial`                |
  | 12     | 1    | `game_opponenttransmission`            |
  | 13     | 9    | `game_trackname` (e.g. `ZCT5`)         |
  | 22     | 2    | `game_framespersec` (10 or 20)         |
  | 24     | 2    | `game_recordedframes`                  |

- **Input block** — the recorded per-frame inputs; the remainder of the
  `recordedframes + 0x724` bytes.

The header carries the track, cars and frame rate, which is why a replay can be
booted directly and why playback uses the recorded simulation rate.

### `.RP2` trailer (16 bytes, little-endian)

Appended immediately after the standard bytes:

| Offset | Size | Field                                     |
| ---    | ---  | ---                                       |
| 0      | 4    | magic `"RP2\0"` (`52 50 32 00`)           |
| 4      | 1    | version (currently `1`)                   |
| 5      | 1    | `fps` (10 or 20)                          |
| 6      | 2    | `total` — finish time in frames           |
| 8      | 2    | `penalty` — penalty time in frames        |
| 10     | 2    | `recordedframes` — copy of the header value |
| 12     | 2    | checksum — sum of bytes 0..11 (mod 65536) |
| 14     | 2    | reserved (`0`)                            |

All times are **frame counts** at `fps`; convert with
`minutes = total/(60*fps)`, `seconds = total/fps`, `centiseconds = (100/fps)*remainder`.
`total = frame + penalty + base` and the clean driving time is
`total - penalty` (see [TIMING.md](TIMING.md)).

## Backwards compatibility

The original game reads a whole replay file into its track buffer (``0x6bf3``
bytes, `seg000.asm:112`) and only interprets the replay part; the trailing
bytes are ignored. There is ~20 KB of slack after the replay, so the 16-byte
trailer never overflows. A `.RP2` **renamed to `.RPL` therefore plays unmodified
in the original** (and in `restunto.exe`).

The plain `.RPL` is written separately so an enhanced file is never required.

## Implementation notes

- The trailer is assembled in a **private `mmgr` allocation** together with the
  replay bytes and written with one `file_write_fatal` call. It is deliberately
  **not** appended into the live track buffer: doing so clobbers the following
  track regions (e.g. `td16_rpl_buffer`) that the game still needs, which made
  the high-score load fail.
- The output filename is built in the scratch `g_path_buf`, **not** `TPath`
  (the track-path prefix the high-score / next-track load relies on).

## Reference parser

```js
// bytes: Uint8Array of the whole .RP2 file
function parseRp2(bytes) {
  const n = bytes.length;
  if (n < 16) return null;
  const t = bytes.slice(n - 16);                 // trailer is the last 16 bytes
  if (!(t[0] === 0x52 && t[1] === 0x50 && t[2] === 0x32 && t[3] === 0)) return null;
  const fps     = t[5] || 20;
  const total   = t[6]  | (t[7]  << 8);
  const penalty = t[8]  | (t[9]  << 8);
  const recorded= t[10] | (t[11] << 8);
  let sum = 0;
  for (let i = 0; i < 12; i++) sum = (sum + t[i]) & 0xffff;
  if ((t[12] | (t[13] << 8)) !== sum) return null; // checksum mismatch
  return { fps, total, penalty, recorded };
}
```

This is the same logic the web demo uses (`web/index.html`) to show the race
time.
