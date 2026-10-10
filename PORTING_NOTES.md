# Porting notes

Hard-won knowledge for working on this codebase (Stunts disassembly/port).
Read this before changing anything under `src/`.

## Build

```sh
export WATCOM=$HOME/watcom-2
export PATH=$WATCOM/armo64:$PATH
export INCLUDE=$WATCOM/h

make -C src c     # compile the C sources into build/c/dos16/librestunts.a
make -C src       # build asm + link -> build/platform/dos16/restunts.exe
```

Outputs:

- `build/platform/dos16/restunts.exe` — the **port** (`RESTUNTS_ORIGINAL`
  *not* defined). Handy aliases / ported C functions are used.
- `build/platform/dos16/restunto.exe` — the **original**: asm only, no port
  (assembled with `-dRESTUNTS_ORIGINAL`). Keep it byte-faithful to the
  original; use it as the reference for regressions.

### Rebuild gotchas (important)

- `src/asm/Makefile` only lists `structs.inc` in `INCLUDES`. Changes to any
  other `segNNN.inc` / `segNNN.def` do **not** trigger recompilation of the
  `.o` files that include them. Force a rebuild by touching the matching
  `.asm`, or by deleting the build tree:
  `rm -rf build/asm/port build/asm/orig build/c/dos16 && make -C src`.
- `make -C src c` is a separate step; `make -C src` alone will not recompile
  changed C files if `librestunts.a` already exists.

## Segment size invariant

Every translated segment must assemble to **exactly** the size of the original
binary. `src/asm/Makefile` (`SEG_SIZES`) enforces this and fails the build
otherwise (e.g. `seg012 size 77C6 does not match target size 7793`).

Consequences:

- You cannot freely add code or data. If you must add something, remove equal
  bytes elsewhere, or **pad with `nop`/`db` so the size is unchanged**.
- Enlarging a segment silently shifts the memory layout; even if you disable
  the check, the game will not boot (black screen). Do **not** disable the
  check for anything other than a throwaway diagnostic.
- Do port-specific edits with `ifdef`/`ifndef RESTUNTS_ORIGINAL` so the
  original build stays byte-faithful.

## Function aliasing and porting a function to C

Functions that *can* be ported are defined in asm as `<name>_asm_` and the
plain `<name>` is provided by an **alias**. There are two copies of that alias
table, and **both** matter:

- `src/asm/segNNN.inc` — used by the segment's own `.asm`.
- `src/asm/segNNN.def` — included by *other* segments (e.g. `seg000.inc`
  pulls in `seg001.def … segNNN.def`).

Each table has an `ifdef RESTUNTS_ORIGINAL` / `else` split:

- the **original** branch does `name = name_asm_` (alias to asm), and
- the **port** branch does `extrn name:proc` for ported functions.

To port a function (replace its asm with a C implementation):

1. Add it to `PORTED_FUNCS_BY_FILE` in `ghidra/restunts-export.py` and
   re-run the Ghidra export so the asm proc is renamed to `<name>_asm_`.
2. In the **port** branch of `segNNN.inc` **and** `segNNN.def`, change
   `name = name_asm_` to `extrn name:proc`.
3. Remove the `ALIAS name = name_asm_` entry from
   `src/platform/dos16/aliases-port.lnk` (the wlink alias list).

Miss any of these and callers silently keep binding to the asm version — this
exact trap is why an early memory-manager hook had no effect: the `.def` alias
included by every other segment was left in place.

**Warning:** if a symbol is defined in C (`extrn name:proc`) *and* an
`ALIAS name = name_asm_` is still present in `aliases-port.lnk`, `wlink`
**aborts with no diagnostic** (`Trace/BPT trap` / `Abort trap`). If a link
starts crashing out of nowhere, check for a duplicate alias first.

Only rename/edit the aliases referenced by the **port** build; leave the
original branch alone.

## Far pointer / calling conventions

- Ported functions use `__cdecl16far` (far call, cdecl arg order). Declare C
  wrappers with the matching `cdecl` and pointer types.
- A far pointer in memory is `offset` (low word) followed by `segment` (high
  word); as a 32-bit value it is `(segment << 16) | offset`. So `p >> 16` is
  the segment.
- `externs.h` may declare asm globals with `#pragma aux sym "*"` (keep the
  symbol name) and `extern`; dseg-resident data must be tagged with
  `#pragma data_seg/bss_seg(dseg, "STUNTSD")`.
- Functions resolved via `aliases-port.lnk` are the *unported* asm helpers.

## The memory-manager "BLOCK NOT FOUND" crash

`src/asm/seg012.asm` is the memory manager. `mmgr_free_asm_`,
`mmgr_release_asm_`, `mmgr_get_chunk_size_asm_`, `mmgr_resize_memory_asm_` and
`mmgr_op_unk_asm_` all `jmp near ptr loc_31498` when a block is not found;
`loc_31498` calls `fatal_error("memory manager - BLOCK NOT FOUND at SEG= %x")`.

Returning to the main menu after a race (finish **or** ESC-quit) frees a
bogus **segment-0** pointer and aborts the game here. Notes on the fix:

- Growing `seg012` to add instrument/logging breaks the size invariant and the
  game will not boot.
- Routing the five functions through C wrappers fixes the crash but **kills
  sound** (the audio path is disturbed by the extra call layer).
- The working fix is a **size-neutral patch of `loc_31498` itself**, port-only,
  that returns cleanly (`mov sp,bp / pop bp / retf` + `nop` padding to the
  original 12 bytes) instead of fataling. All six callers share a
  `push bp / mov bp,sp` frame, so this unwinds correctly for each.

## CLI / boot features (port-only)

`src/c/replayboot.c` implements a `/tTRACK` argument (e.g. `/tDEFAULT`) that
boots straight into a race on the named track:

- `run_intro_looped` runs the **original** intro (it auto-advances); `run_menu`
  loads the requested track then calls the original menu with `idle_expired`
  forced, which makes it auto-select "Let's Drive".
- Wiring: `seg000.def`/`seg000.inc` declare `run_intro_looped`/`run_menu` as
  `extrn`, and their `ALIAS` entries are removed from `aliases-port.lnk`.

## Web demo (js-dos)

`web/` builds browser bundles (`make-bundles.py`, one `.jsdos` per track) and
serves them with COOP/COEP headers (`serve.py`) so `SharedArrayBuffer` works.
Bust the js-dos IndexedDB bundle cache with the `?v=N` query in `index.html`.
