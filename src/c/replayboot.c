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

// Unported asm helpers, resolved via aliases-port.lnk.
extern void cdecl file_build_path(const char* dir, const char* name, const char* ext, char* dst);
// dseg-resident scratch path (asm file I/O takes near pointers).
#pragma aux g_path_buf "*"
extern char g_path_buf[];
// Existing dseg symbols used to build the track path exactly like the game
// does; this avoids putting new strings in dseg (which breaks audio).
#pragma aux TPath "*"
extern char TPath[];
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
static char boot_track[9];
static char boot_car[4];

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
            while (i <= len && tail[i] != ' ' && tail[i] != '\t' && tail[i] != '\r' && tail[i] != '/') {
                i++;
            }
        }
    }
}

uint16_t cdecl run_intro_looped(void)
{
    if (!boot_parsed) {
        boot_parse_cmdline();
    }
    // Always run the original intro. It auto-advances on its own (its input
    // wait times out), and running it keeps the game fully initialized.
    return run_intro_looped_asm_();
}

int cdecl run_menu(void)
{
    uint16_t k;
    int r;

    if (!boot_parsed) {
        boot_parse_cmdline();
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
