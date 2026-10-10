# Stunts timing

How race timing is represented and computed in this build. All references are
`file:line` into `src/`.

## Time base: frames

The race is simulated at a fixed rate and **everything is counted in frames**;
seconds only appear when formatting for display.

- `framespersec` (global, `src/asm/dsegu.asm`) is the current simulation rate:
  **20** by default (`seg000.asm:2577`), or **10** — there are separate
  `steerWhlRespTable_10fps` / `steerWhlRespTable_20fps` physics tables chosen by
  `framespersec` (`seg001.asm:3837`).
- The rate is stored per race in `gameconfig.game_framespersec`
  (`GAMEINFO`, `src/c/externs.h:145`), which is part of the replay header, so a
  replay plays back at the rate it was recorded.
- Each simulation frame does `inc state.game_frame` (`seg001.asm:4424`).

Frames are turned into `M:SS.hh` by `format_frame_as_string(frames,
centiseconds_flag, buf)` (`seg008.asm:4221`):

```
minutes      = frames / (60 * framespersec)
frames      -= minutes * 60 * framespersec
seconds      = frames / framespersec
frames      -= seconds * framespersec        // remainder = sub-second frames
centiseconds = (100 / framespersec) * frames // only if the flag is set
```

## Total (finish) time

Set when the player car crosses the finish (`seg001.asm:8818`):

```
state.game_total_finish = state.game_frame    // frames driven
                        + state.game_penalty   // accumulated penalty frames
                        + elapsed_time1        // base/carry (0 for a fresh race)
```

- `elapsed_time1` is zeroed when a fresh race is initialised
  (`seg001.asm:3810`); it is a base offset for the run (e.g. staged/
  competition starts).
- For the opponent car the total is stored without penalty:
  `state.field_144 = state.game_frame + elapsed_time1` (`seg001.asm:8830`).
- At finish the display rate `state.game_frames_per_sec` is set to
  `framespersec * 4` (`seg001.asm:8811`), used by the evaluation screen.

The results screen shows the **clean** driving time (`seg000.asm:5147`):

```
format_frame_as_string(gState_total_finish_time - gState_penalty, /*cs*/ 1, buf)
```

i.e. the penalty is subtracted out of the displayed time and reported
separately.

## Penalty timing

Penalties are the time added for leaving the track / driving on invalid ground.

`detect_penalty(char* state, int* penalty_counter)` (`seg001.asm:5267`) is a
flood-fill / graph search over the track pieces. It reads the player's world
cell from `state.playerstate.car_posWorld1` and the track maps
(`td01_track_file_cpy`, `td22_row_from_path`, `td21_col_from_path`,
`td17_trk_elem_ordered`, `trkObjectList`) on a ~30x30 grid
(`game_startcol/row`):

- car inside the grid, on/near its start cell -> `penalty_counter = 0`
  (`seg001.asm:5322`);
- car outside the grid -> `penalty_counter = -2` (`0xfffe`, `seg001.asm:5407`);
- otherwise it walks the piece graph and returns the distance (in pieces) back
  to a valid path as `penalty_counter`.

The result is applied per frame (`seg001.asm:4764`) only when the counter is
positive:

```
penalty_time         = 3 * penalty_counter * framespersec   // frames
show_penalty_counter = 4 * framespersec                     // frames banner is shown
state.game_penalty  += penalty_time
```

So each penalty step adds `3 * framespersec` frames (= 3 seconds at 20 fps).
`show_penalty_counter` is counted down one per frame (`seg001.asm:4603`) to keep
the on-screen penalty banner visible for `4 * framespersec` frames.

Related globals: `penalty_time`, `penalty_counter`, `show_penalty_counter`
(`src/asm/dsegu.asm`).

## Replay timing

A replay is the recorded inputs plus the race's timing configuration.

- `gameconfig.game_recordedframes` is the total number of frames of the race,
  incremented each frame while recording (`seg005.asm:1450`).
- A `.RPL` file = a **13-word header**, which is exactly the `GAMEINFO` struct
  (`src/c/externs.h:136`) — playercarid/materials/transmission, opponent
  id/material/transmission, trackname, `game_framespersec`,
  `game_recordedframes` — followed by the input block.
- **Write** (`file_write_replay`, `seg005.asm:1872`) copies `gameconfig` (13
  words) into the header and writes `game_recordedframes + 0x724` bytes.
- **Load** (`file_load_replay`, `seg005.asm:1829`) copies the 13-word header
  back into `gameconfig` with `rep movsw` (`seg005.asm:1861`); the race then
  runs `game_recordedframes` frames feeding the recorded inputs (replay mode).

Because the header carries `game_framespersec`, playback uses the same
simulation rate as the recording, and because it carries the track/car, a
replay can be booted directly (see the `FILE` CLI argument in
[README.md](README.md) and the implementation in `src/c/replayboot.c`).

## Enhanced replays (`.RP2`)

Every finished race is auto-saved twice: `lastrep.rpl` (standard) and
`lastrep.rp2` (the standard bytes plus a 16-byte timing trailer carrying the
total and penalty frames). The trailer, the underlying `.RPL` byte layout and
the backwards-compatibility guarantees are specified in
**[RP2_FILE_FORMAT.md](RP2_FILE_FORMAT.md)**.

The clean driving time is `total - penalty`; both are frame counts at `fps`,
formatted as `M:SS.hh` with the game's `format_frame_as_string`
(centiseconds = `(100/fps) * remainder`).

## Key fields (see `src/c/externs.h`)

| Field                              | Meaning                                         |
| ---                                | ---                                             |
| `GAMESTATE.game_frame`             | frames driven in the current race               |
| `GAMESTATE.game_penalty`           | accumulated penalty frames                      |
| `GAMESTATE.game_total_finish`      | finish time in frames (frame + penalty + base)  |
| `GAMESTATE.game_frame_in_sec`      | working "seconds" counter (eval/staging)        |
| `GAMESTATE.game_frames_per_sec`    | frames-per-second used by the eval screen       |
| `GAMEINFO.game_framespersec`       | simulation rate (10/20), stored in the replay   |
| `GAMEINFO.game_recordedframes`     | total race frames, stored in the replay         |
| `gState_total_finish_time`         | copy of the finish time used for display        |
| `framespersec`                     | global current simulation rate                  |
| `penalty_time`/`penalty_counter`   | last penalty duration / steps                   |
| `show_penalty_counter`             | frames left to show the penalty banner          |
