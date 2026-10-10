// Boot straight into content given on the DOS command line:
//
//   restunts.exe FUN            boot into replay FUN (".rpl" optional)
//   restunts.exe /cJAGU /tZCT5  race Jaguar on track ZCT5, skipping menus
//   restunts.exe FUN /cJAGU     watch FUN with the player car swapped
//
// A replay header fills gameconfig (track, cars, frame count), so the
// regular flow (track load, run_game) enters replay mode without showing
// the intro or menu first. This mirrors the in-game "watch replay" path:
// skip intro (return 0), skip menu (return 4 = option menu), then load the
// replay and return nonzero from the option menu (var_A = 1 = replay mode).
// A CLI-booted replay rewinds to frame 0 at the end and loops (attract
// mode), and finished live races are auto-saved as "lastrep".
//
// Integration notes (see also PORTED_FUNCS_BY_FILE in
// ghidra/restunts-export.py):
// - These live in another segment than seg000, so seg000.asm must call them
//   with `call far ptr` (the original `push cs` + `call near ptr` cannot
//   reach across segments). Three filler NOPs were removed elsewhere in
//   seg000 to keep its size at the checked 0x4712.
// - PORTED_FUNCS_BY_FILE lists them so a future Ghidra re-export keeps the
//   `extrn` overrides in seg000.inc/seg000.def and out of aliases-port.lnk.

#include <dos.h>
#include <stdint.h>

#include "externs.h"
#include "restunts.h"

// Original implementations (renamed with _asm_ suffix).
extern uint16_t cdecl run_intro_looped_asm_(void);
extern int cdecl run_menu_asm_(void);
extern int cdecl run_option_menu_asm_(void);

// Unported asm helpers, resolved via aliases-port.lnk.
extern int16_t cdecl file_load_replay(const char* dir, const char* name);
extern void cdecl file_build_path(const char* dir, const char* name, const char* ext, char* dst);
// dseg-resident scratch path (asm file I/O takes near pointers).
#pragma aux g_path_buf "*"
extern char g_path_buf[];

// All file-scope data must live in the game's data segment: at runtime DS
// points at dseg, and the asm helpers take near (DS-relative) pointers, so
// data in Watcom's default _DATA/_BSS segments is unreachable.
#pragma data_seg( dseg, "STUNTSD" )
#pragma bss_seg( dseg, "STUNTSD" )

static int boot_parsed = 0;
static int boot_pending = 0;
// Set when a CLI boot enters replay mode: at the exact end of the replay,
// rewind to frame 0 and keep playing (attract-mode loop, same as pressing
// the viewer's rewind and play buttons).
static int boot_autoplay = 0;
// dseg-resident constant string (asm file I/O takes near pointers).
// NOTE: plain initializers do NOT survive: the CRT startup zeroes all of
// dseg from 0x55ca upward, including our appended data. Fill at runtime.
static char boot_trkext[5];
// Requested car (4 uppercase chars) / track (up to 8) and their flags.
static char boot_car[4];
static int boot_car_set = 0;
static char boot_track[9];
static int boot_track_set = 0;
// Scratch for the token being parsed (never read as a value).
static char boot_tok[10];
// Near (DS-relative) storage, as required by file_load_replay.
static char boot_replay_name[9];
static char boot_empty_str[1];

// PSP segment via INT 21h/62h. Fully explicit registers, no library call,
// avoiding any calling-convention interaction.
static unsigned boot_psp_seg(void);
#pragma aux boot_psp_seg = \
    "mov ah, 062h" \
    "int 021h" \
    "mov ax, bx" \
    value [ax] modify [ax bx cx dx es];

// Uppercase a buffer in place (DOS filenames are uppercase).
static void boot_upper(char* buf, uint16_t n)
{
    uint16_t k;
    for (k = 0; k < n; k++) {
        if (buf[k] >= 'a' && buf[k] <= 'z') {
            buf[k] -= 'a' - 'A';
        }
    }
}

// DOS command line: first non-/ token names a replay (without trailing
// .rpl, max 8 chars); /cXXXX selects the player car, /tXXXXXXXX the track.
// Everything is uppercased since DOS filenames are uppercase.
static void boot_parse_cmdline(void)
{
    uint8_t far* tail;
    uint16_t len;
    uint16_t i;
    uint16_t n;
    uint16_t k;
    int ok;

    boot_parsed = 1;

    // Runtime-init the constant string (see note at its declaration).
    boot_trkext[0] = '.';
    boot_trkext[1] = 't';
    boot_trkext[2] = 'r';
    boot_trkext[3] = 'k';
    boot_trkext[4] = 0;

    tail = MK_FP(boot_psp_seg(), 0x80);
    len = tail[0];
    if (len > 127) {
        len = 127;
    }

    i = 1; // tail[1] is usually a blank.
    for (;;) {
        while (i <= len && (tail[i] == ' ' || tail[i] == '\t' || tail[i] == '\r')) {
            i++;
        }
        if (i > len) {
            return;
        }
        if (tail[i] == '/') {
            // /cXXXX car or /tXXXXXXXX track switch.
            n = 0;
            i++;
            while (i <= len && n < 9 && tail[i] != ' ' && tail[i] != '\t' && tail[i] != '\r' && tail[i] != '/') {
                boot_tok[n++] = tail[i++];
            }
            boot_tok[n] = 0;
            if (n < 2) {
                continue;
            }
            if (boot_tok[0] == 'c' || boot_tok[0] == 'C') {
                // Exactly 4 identifier chars.
                if (n != 5) {
                    continue;
                }
                ok = 1;
                for (k = 1; k < 5; k++) {
                    boot_car[k - 1] = boot_tok[k];
                    if (!((boot_car[k - 1] >= 'a' && boot_car[k - 1] <= 'z')
                        || (boot_car[k - 1] >= 'A' && boot_car[k - 1] <= 'Z')
                        || (boot_car[k - 1] >= '0' && boot_car[k - 1] <= '9'))) {
                        ok = 0;
                    }
                }
                if (ok) {
                    boot_upper(boot_car, 4);
                    boot_car_set = 1;
                }
            } else if (boot_tok[0] == 't' || boot_tok[0] == 'T') {
                // 1-8 identifier chars.
                if (n < 2 || n > 9) {
                    continue;
                }
                ok = 1;
                for (k = 1; k < n; k++) {
                    boot_track[k - 1] = boot_tok[k];
                    if (!((boot_track[k - 1] >= 'a' && boot_track[k - 1] <= 'z')
                        || (boot_track[k - 1] >= 'A' && boot_track[k - 1] <= 'Z')
                        || (boot_track[k - 1] >= '0' && boot_track[k - 1] <= '9'))) {
                        ok = 0;
                    }
                }
                if (ok) {
                    boot_upper(boot_track, n - 1);
                    boot_track[n - 1] = 0;
                    boot_track_set = 1;
                }
            }
        } else {
            // Replay name (first one wins).
            if (!boot_pending) {
                n = 0;
                while (i <= len && n < 8 && tail[i] != ' ' && tail[i] != '\t' && tail[i] != '\r' && tail[i] != '/') {
                    boot_replay_name[n++] = tail[i++];
                }
                // Strip a trailing ".rpl" so NAME and NAME.RPL both work.
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
    if (boot_pending || boot_car_set || boot_track_set) {
        return 0; // Skip intro, go to menu.
    }
    return run_intro_looped_asm_();
}

int cdecl run_menu(void)
{
    uint16_t k;
    if (boot_pending) {
        return 4; // Go to option menu.
    }
    if (boot_car_set || boot_track_set) {
        // Fresh race with preselected car/track: apply once, then drive.
        // (Track load mirrors the outer-loop .TRK load in stuntsmain.)
        if (boot_car_set) {
            boot_car_set = 0;
            for (k = 0; k < 4; k++) {
                gameconfig.game_playercarid[k] = boot_car[k];
            }
        }
        if (boot_track_set) {
            boot_track_set = 0;
            for (k = 0; k < 9; k++) {
                gameconfig.game_trackname[k] = boot_track[k];
                if (boot_track[k] == 0) {
                    break;
                }
            }
            file_build_path(boot_empty_str, gameconfig.game_trackname, boot_trkext, g_path_buf);
            file_read_fatal(g_path_buf, td14_elem_map_main);
        }
        return 0; // Race (var_A = 0 path: track_setup, init, run_game).
    }
    return run_menu_asm_();
}

int cdecl run_option_menu(void)
{
    uint16_t k;
    if (boot_pending) {
        boot_pending = 0;
        boot_autoplay = 1;
        file_load_replay(boot_empty_str, boot_replay_name);
        // A /c switch overrides the replay's player car.
        if (boot_car_set) {
            boot_car_set = 0;
            for (k = 0; k < 4; k++) {
                gameconfig.game_playercarid[k] = boot_car[k];
            }
        }
        boot_track_set = 0; // The replay's own track always wins.
        return 1; // Nonzero: enter replay mode (var_A = 1).
    }
    return run_option_menu_asm_();
}

// Timer tick ported from frame_callback_asm_ (seg005.asm), with autoplay
// and auto-save tails for CLI boots. Called from interrupt context via
// timer_reg_callback, like the original.
void cdecl frame_callback(void)
{
    if (compare_ds_ss() == 0) {
        return;
    }
    if (byte_442E4 != 0) {
        return;
    }
    byte_442E4++;
    if (byte_442E4 == 1) {
        word_443F4++;
        if (word_443F4 >= word_4499C) {
            if (word_44D1E != word_449E4) {
                sub_18D06((int16_t)(0x22 * (int32_t)word_44D1E + 0x97dc), word_443F4);
                word_443F4 = 0;
                word_44D1E++;
                if (word_44D1E == 0x28) {
                    word_44D1E = 0;
                }
            }
        }
        if (gabort == 0) {
            if (askfull == 0) {
                if (is_in_replay == 0 || game_replay_mode != 2) {
                    // NOTE: mode 0 with frame_in_sec still below
                    // frames_per_sec falls through to the tick body below
                    // (it does NOT skip it).
                    if (game_replay_mode == 0
                        && state.game_frame_in_sec >= state.game_frames_per_sec) {
                        is_in_replay = 1;
                        audio_carstate();
                    } else {
                        byte_44A8A--;
                        if (byte_44A8A == 0) {
                            byte_44A8A = (uint8_t)word_4499C;
                            word_46468++;
                            if (game_replay_mode == 2) {
                                if ((int16_t)(int8_t)rspeed == 2) {
                                    byte_4552F--;
                                    if (byte_4552F == 0) {
                                        replay_unk2(0);
                                        byte_4552F = 2;
                                    }
                                } else {
                                    replay_unk2(0);
                                }
                            } else {
                                replay_unk2(0);
                            }
                        }
                    }
                }
            }
        }
    }
    byte_442E4--;
    if (boot_autoplay
        && game_replay_mode == 2
        && gameconfig.game_recordedframes != 0
        && state.game_frame == (int16_t)gameconfig.game_recordedframes
        && elapsed_time2 == gameconfig.game_recordedframes) {
        restore_gamestate(0);
        is_in_replay = 0;
    }
}
