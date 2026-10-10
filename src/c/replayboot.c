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
// Original asm replay writer/loader (seg005.asm).
extern int cdecl file_write_replay_asm_(const char* filename);
extern int16_t cdecl file_load_replay(const char* dir, const char* name);

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

void cdecl run_game(void)
{
    uint16_t k;

    run_game_asm_();

    if (gameconfig.game_recordedframes == 0) {
        return; // nothing recorded (e.g. a replay-only session)
    }
    // Build the name in the dseg scratch buffer: the asm file I/O wants a
    // near pointer, and we must not add new data to dseg (that breaks audio).
    for (k = 0; k < sizeof(boot_repname); k++) {
        g_path_buf[k] = boot_repname[k];
    }
    file_write_replay_asm_(g_path_buf);
}
