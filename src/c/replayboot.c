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
// Main-loop idle flag. Setting it makes the original menu auto-select
// "Let's Drive" (see run_menu_asm_), so we can fast-forward through the menu
// while still running all of its initialization.
#pragma aux idle_expired "*"
extern uint8_t idle_expired;

// Hook the original memory manager free/release so a bogus (segment 0) free
// does not abort the game. Original asm implementations (see seg012).
extern void far* cdecl mmgr_free_asm_(void far* data);
extern void far* cdecl mmgr_release_asm_(void far* data);

void far* cdecl mmgr_free(void far* data)
{
    if ((((unsigned long)data) >> 16) == 0) {
        _printf("mmgr_free seg0 data=%08lX\n", (unsigned long)data);
        return data;
    }
    return mmgr_free_asm_(data);
}

void far* cdecl mmgr_release(void far* data)
{
    if ((((unsigned long)data) >> 16) == 0) {
        _printf("mmgr_release seg0 data=%08lX\n", (unsigned long)data);
        return data;
    }
    return mmgr_release_asm_(data);
}

extern unsigned int cdecl mmgr_get_chunk_size_asm_(void far* data);
extern unsigned int cdecl mmgr_resize_memory_asm_(void far* data, unsigned int size_);
extern void far* cdecl mmgr_op_unk_asm_(void far* data);

unsigned int cdecl mmgr_get_chunk_size(void far* data)
{
    if ((((unsigned long)data) >> 16) == 0) {
        _printf("mmgr_get_chunk_size seg0 data=%08lX\n", (unsigned long)data);
        return 0;
    }
    return mmgr_get_chunk_size_asm_(data);
}

unsigned int cdecl mmgr_resize_memory(void far* data, unsigned int size_)
{
    if ((((unsigned long)data) >> 16) == 0) {
        _printf("mmgr_resize_memory seg0 data=%08lX\n", (unsigned long)data);
        return 0;
    }
    return mmgr_resize_memory_asm_(data, size_);
}

void far* cdecl mmgr_op_unk(void far* data)
{
    if ((((unsigned long)data) >> 16) == 0) {
        _printf("mmgr_op_unk seg0 data=%08lX\n", (unsigned long)data);
        return data;
    }
    return mmgr_op_unk_asm_(data);
}

// File-scope data must live in the game's data segment (DS points at dseg
// at runtime; the asm helpers take near pointers).
#pragma data_seg( dseg, "STUNTSD" )
#pragma bss_seg( dseg, "STUNTSD" )

static int boot_parsed = 0;
static int boot_track_set = 0;
static char boot_track[9];
// dseg-resident ".trk" (initializers don't survive CRT startup zeroing).
static char boot_trkext[5];
static char boot_empty_str[1];

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

// Parse the DOS command line for /tXXXXXXXX (track name, 1-8 chars).
static void boot_parse_cmdline(void)
{
    uint8_t far* tail;
    uint16_t len, i, n, k;
    int ok;

    boot_parsed = 1;

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
                boot_track[n++] = tail[i++];
            }
            if (n >= 2 && (boot_track[0] == 't' || boot_track[0] == 'T')) {
                ok = 1;
                for (k = 1; k < n; k++) {
                    boot_track[k - 1] = boot_track[k];
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
    if (boot_track_set) {
        // Apply the requested track once, then let the original menu
        // fast-forward itself into "Let's Drive".
        boot_track_set = 0;
        for (k = 0; k < 9; k++) {
            gameconfig.game_trackname[k] = boot_track[k];
            if (boot_track[k] == 0) {
                break;
            }
        }
        file_build_path(boot_empty_str, gameconfig.game_trackname, boot_trkext, g_path_buf);
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
