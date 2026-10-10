// Boot a race on a track given on the DOS command line, e.g.
//   restunts.exe /tZCT5
// This skips the intro and menu and loads the named track, then starts the
// race. Nothing else (no car selection, no replay loading).

#include <dos.h>
#include <stdint.h>

#include "externs.h"
#include "restunts.h"

// Original implementations in seg000.asm (renamed with _asm_ suffix).
extern uint16_t cdecl run_intro_looped_asm_(void);
extern int cdecl run_menu_asm_(void);
extern int cdecl run_option_menu_asm_(void);
extern void cdecl run_game_asm_(void);
// Original asm replay writer/loader (seg005.asm) and raw file writer (seg012).
extern int cdecl file_write_replay_asm_(const char* filename);
extern int16_t cdecl file_load_replay(const char* dir, const char* name);
extern void cdecl file_write_fatal_asm_(const char* filename, void far* src, unsigned long len);
// Memory manager (seg012): mmgr_alloc_resbytes is declared in externs.h.
extern void far* cdecl mmgr_free_asm_(void far* data);

// Unported asm helpers, resolved via aliases-port.lnk.
extern void cdecl file_build_path(const char* dir, const char* name, const char* ext, char* dst);
// dseg-resident scratch path (asm file I/O takes near pointers).
#pragma aux g_path_buf "*"
extern char g_path_buf[];
// Existing dseg symbols used to build the track path exactly like the game
// does; this avoids putting new strings in dseg (which breaks audio).
#pragma aux TPath "*"
extern char TPath[];
#pragma aux RPath "*"
extern char RPath[];
#pragma aux a_trk "*"
extern char a_trk[];
// Main-loop idle flag. Setting it makes the original menu auto-select
// "Let's Drive" (see run_menu_asm_), so we can fast-forward through the menu
// while still running all of its initialization.
#pragma aux idle_expired "*"
extern uint8_t idle_expired;
// Current simulation rate (10 or 20 fps); stored in the enhanced trailer.
#pragma aux framespersec "*"
extern uint16_t framespersec;

// NOTE: do NOT force this data into the game's `dseg` segment. Growing `dseg`
// shifts the game's memory layout and silently breaks audio. Like math.c, let
// the compiler place it in its own data segment; that segment is part of the
// same DGROUP as dseg, so the asm helpers (which access DS-relative near
// pointers) still read these strings correctly at runtime.

static int boot_parsed = 0;
static int boot_track_set = 0;
static int boot_car_set = 0;
static int boot_pending = 0;
static char boot_track[9];
static char boot_car[4];
static char boot_replay_name[9];

// PSP segment via INT 21h/62h (explicit registers, no lib call).
static unsigned boot_psp_seg(void);
#pragma aux boot_psp_seg = \
    "mov ah, 062h" \
    "int 021h" \
    "mov ax, bx" \
    value [ax] modify [ax bx cx dx es];

static void boot_upper(char* buf, uint16_t n)
{
    uint16_t k;
    for (k = 0; k < n; k++) {
        if (buf[k] >= 'a' && buf[k] <= 'z') {
            buf[k] -= 'a' - 'A';
        }
    }
}

// Parse the DOS command line for /cXXXX (car id, exactly 4 chars) and
// /tXXXXXXXX (track name, 1-8 chars).
static void boot_parse_cmdline(void)
{
    uint8_t far* tail;
    uint16_t len, i, n, k;
    char tok[9];
    int ok;

    boot_parsed = 1;

    tail = MK_FP(boot_psp_seg(), 0x80);
    len = tail[0];
    if (len > 127) {
        len = 127;
    }

    i = 1;
    for (;;) {
        while (i <= len && (tail[i] == ' ' || tail[i] == '\t' || tail[i] == '\r')) {
            i++;
        }
        if (i > len) {
            return;
        }
        if (tail[i] == '/') {
            n = 0;
            i++;
            while (i <= len && n < 9 && tail[i] != ' ' && tail[i] != '\t' && tail[i] != '\r' && tail[i] != '/') {
                tok[n++] = tail[i++];
            }
            if (n < 2) {
                continue;
            }
            ok = 1;
            for (k = 1; k < n; k++) {
                if (!((tok[k] >= 'a' && tok[k] <= 'z')
                    || (tok[k] >= 'A' && tok[k] <= 'Z')
                    || (tok[k] >= '0' && tok[k] <= '9'))) {
                    ok = 0;
                }
            }
            if (!ok) {
                continue;
            }
            if (tok[0] == 'c' || tok[0] == 'C') {
                if (n != 5) {
                    continue; // car id is exactly 4 chars
                }
                for (k = 0; k < 4; k++) {
                    boot_car[k] = tok[k + 1];
                }
                boot_upper(boot_car, 4);
                boot_car_set = 1;
            } else if (tok[0] == 't' || tok[0] == 'T') {
                for (k = 1; k < n; k++) {
                    boot_track[k - 1] = tok[k];
                }
                boot_upper(boot_track, n - 1);
                boot_track[n - 1] = 0;
                boot_track_set = 1;
            }
        } else {
            // A bare token (not /c, /t) names a replay file (first one wins).
            if (!boot_pending) {
                n = 0;
                while (i <= len && n < 8 && tail[i] != ' ' && tail[i] != '\t' && tail[i] != '\r' && tail[i] != '/') {
                    boot_replay_name[n++] = tail[i++];
                }
                if (n > 4
                    && boot_replay_name[n - 4] == '.'
                    && (boot_replay_name[n - 3] == 'r' || boot_replay_name[n - 3] == 'R')
                    && (boot_replay_name[n - 2] == 'p' || boot_replay_name[n - 2] == 'P')
                    && (boot_replay_name[n - 1] == 'l' || boot_replay_name[n - 1] == 'L')) {
                    n -= 4;
                }
                boot_upper(boot_replay_name, n);
                boot_replay_name[n] = 0;
                if (n != 0) {
                    boot_pending = 1;
                }
            } else {
                while (i <= len && tail[i] != ' ' && tail[i] != '\t' && tail[i] != '\r' && tail[i] != '/') {
                    i++;
                }
            }
        }
    }
}

uint16_t cdecl run_intro_looped(void)
{
    if (!boot_parsed) {
        boot_parse_cmdline();
    }
    // Fast-forward the intro when a CLI boot target was given.
    if (boot_pending || boot_car_set || boot_track_set) {
        return 0;
    }
    // Otherwise run the original intro (it auto-advances on its own).
    return run_intro_looped_asm_();
}

int cdecl run_menu(void)
{
    uint16_t k;
    int r;

    if (!boot_parsed) {
        boot_parse_cmdline();
    }
    if (boot_pending) {
        return 4; // -> option menu, where the replay is loaded
    }
    if (boot_track_set || boot_car_set) {
        // Apply the requested car and/or track once, then let the original
        // menu fast-forward itself into "Let's Drive".
        boot_track_set = 0;
        if (boot_car_set) {
            boot_car_set = 0;
            for (k = 0; k < 4; k++) {
                gameconfig.game_playercarid[k] = boot_car[k];
            }
        } else {
            // Default to PMIN (the fastest car most people use).
            gameconfig.game_playercarid[0] = 'P';
            gameconfig.game_playercarid[1] = 'M';
            gameconfig.game_playercarid[2] = 'I';
            gameconfig.game_playercarid[3] = 'N';
        }
        if (boot_track[0] != 0) {
            for (k = 0; k < 9; k++) {
                gameconfig.game_trackname[k] = boot_track[k];
                if (boot_track[k] == 0) {
                    break;
                }
            }
        }
        file_build_path(TPath, gameconfig.game_trackname, a_trk, g_path_buf);
        file_read_fatal(g_path_buf, td14_elem_map_main);
        // The original menu auto-selects menu item 0 ("Let's Drive") when the
        // idle flag is set, after doing all of its normal setup.
        idle_expired = 1;
        r = run_menu_asm_();
        idle_expired = 0;
        return r;
    }
    return run_menu_asm_();
}

// Replay viewing. When a replay name is given on the CLI, the main loop routes
// run_menu()==4 -> _do_option_menu -> run_option_menu(); load the replay here
// and return nonzero to enter replay mode. file_load_replay fills gameconfig
// from the .rpl header, so the normal track_load/run_game path plays it back.
int cdecl run_option_menu(void)
{
    uint16_t k;

    if (!boot_parsed) {
        boot_parse_cmdline();
    }
    if (boot_pending) {
        boot_pending = 0;
        // dir ("" into TPath) and name (RPath) must be dseg near strings; the
        // asm loader reads them via DS, and builds the path into g_path_buf.
        TPath[0] = 0;
        for (k = 0; boot_replay_name[k] != 0; k++) {
            RPath[k] = boot_replay_name[k];
        }
        RPath[k] = 0;
        file_load_replay(TPath, RPath);
        if (boot_car_set) {
            boot_car_set = 0;
            for (k = 0; k < 4; k++) {
                gameconfig.game_playercarid[k] = boot_car[k];
            }
        }
        boot_track_set = 0; // the replay's own track wins
        return 1;           // enter replay mode
    }
    return run_option_menu_asm_();
}

// Auto-save the just-finished race's replay to "lastrep.rpl" (no dialog), so
// the web page can pick it up. The replay buffer (td13_rpl_header) is a
// persistent allocation, so it is still valid once run_game() returns.
static const char boot_repname[] = "lastrep.rpl";

// Enhanced replay "lastrep.rp2": the standard .RPL bytes plus a 16-byte timing
// trailer. The original game reads the whole file into its ~27KB track buffer
// (which has ample slack after the replay), so it loads a .RP2 renamed to .RPL
// and simply ignores the trailer. Trailer layout (little-endian):
//   0  magic  "RP2\0"
//   4  version (1)
//   5  fps (1)                     -- framespersec (10/20)
//   6  total finish frames (u16)   -- game_total_finish (frame + penalty + base)
//   8  penalty frames (u16)        -- game_penalty
//   10 recorded frames (u16)       -- game_recordedframes
//   12 checksum (u16)              -- sum of bytes 0..11
//   14 reserved
static const char boot_rep2[] = "lastrep.rp2";

static void boot_write_enhanced(void)
{
    uint8_t far* buf = (uint8_t far*)td13_rpl_header;
    uint8_t far* tmp;
    uint32_t std_len = (uint32_t)gameconfig.game_recordedframes + 0x724UL;
    uint16_t total = (uint16_t)state.game_total_finish;
    uint16_t pen = (uint16_t)state.game_penalty;
    uint16_t rec = gameconfig.game_recordedframes;
    uint8_t trailer[16];
    uint16_t k;
    uint16_t sum;
    uint32_t i;

    trailer[0] = 'R';
    trailer[1] = 'P';
    trailer[2] = '2';
    trailer[3] = 0;
    trailer[4] = 1;
    trailer[5] = (uint8_t)framespersec;
    trailer[6] = (uint8_t)(total & 0xff);
    trailer[7] = (uint8_t)(total >> 8);
    trailer[8] = (uint8_t)(pen & 0xff);
    trailer[9] = (uint8_t)(pen >> 8);
    trailer[10] = (uint8_t)(rec & 0xff);
    trailer[11] = (uint8_t)(rec >> 8);
    sum = 0;
    for (k = 0; k < 12; k++) {
        sum += trailer[k];
    }
    trailer[12] = (uint8_t)(sum & 0xff);
    trailer[13] = (uint8_t)(sum >> 8);
    trailer[14] = 0;
    trailer[15] = 0;

    // Build replay + trailer in a private allocation. Appending into the live
    // replay buffer would clobber the following track regions the game still
    // uses (the file is read/written contiguously from td13_rpl_header).
    tmp = mmgr_alloc_resbytes(a_trk, (int32_t)(std_len + 16UL));
    if (tmp == 0) {
        return;
    }
    for (i = 0; i < std_len; i++) {
        tmp[i] = buf[i];
    }
    for (k = 0; k < 16; k++) {
        tmp[std_len + k] = trailer[k];
    }
    // Use g_path_buf (a scratch path the game rebuilds), NOT TPath: TPath is
    // the track path prefix the high-score/next-track load still relies on.
    for (k = 0; k < sizeof(boot_rep2); k++) {
        g_path_buf[k] = boot_rep2[k];
    }
    file_write_fatal_asm_(g_path_buf, (void far*)tmp, std_len + 16UL);
    mmgr_free_asm_(tmp);
}

void cdecl run_game(void)
{
    uint16_t k;

    run_game_asm_();

    if (gameconfig.game_recordedframes == 0) {
        return; // nothing recorded (e.g. a replay-only session)
    }
    // Plain replay (byte-for-byte the original format; loads in any build).
    for (k = 0; k < sizeof(boot_repname); k++) {
        g_path_buf[k] = boot_repname[k];
    }
    file_write_replay_asm_(g_path_buf);

    // Enhanced replay carrying the timing (our port reads it back).
    boot_write_enhanced();
}
